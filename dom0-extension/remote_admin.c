/* SPDX-License-Identifier: Apache-2.0 */
/* Restricted Dom0 management interface for named test guests. */
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/crc.h>
#include <zephyr/arch/arm64/hypercall.h>
#include <zephyr/xen/dom0/domctl.h>
#include <domain.h>
#include <xen_dom_mgmt.h>
#include <xen_console.h>
#include <xss.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dom0.h"

#define BASE "/local/domain/1/rtt-admin/"
K_THREAD_STACK_DEFINE(admin_stack, 12288);
static struct k_thread admin_thread;
static struct fs_file_t upload;
static bool uploading;
static size_t expected_size, received_size;
static uint32_t expected_crc, received_crc;
static char request[1200], response[3400], output[3072];
static char console_ring[16384];
static size_t console_head, console_tail;
static unsigned console_dropped;
static struct k_spinlock console_lock;
static int linux_pin_rc, dom0_pin_rc, rtt_pin_rc;

/* Exact Xen 4.19 public domctl_vcpuaffinity ABI, absent from the trimmed header. */
struct affinity_payload {
    uint32_t vcpu, flags;
    struct xenctl_bitmap hard, soft;
};
BUILD_ASSERT(sizeof(struct affinity_payload) == 40);

static int pin_vcpu(uint16_t domid, uint32_t vcpu, uint8_t mask)
{
    struct affinity_payload payload = { .vcpu = vcpu, .flags = 1 };
    payload.hard.nr_bits = 8;
    set_xen_guest_handle(payload.hard.bitmap, &mask);
    xen_domctl_t ctl = {
        .cmd = XEN_DOMCTL_setvcpuaffinity,
        .interface_version = CONFIG_XEN_DOMCTL_INTERFACE_VERSION,
        .domain = domid,
    };
    memcpy(ctl.u.pad, &payload, sizeof(payload));
    return HYPERVISOR_domctl(&ctl);
}

static void console_feed(char ch, void *data)
{
    ARG_UNUSED(data);
    k_spinlock_key_t key = k_spin_lock(&console_lock);
    size_t next = (console_head + 1) % sizeof(console_ring);
    if (next == console_tail) {
        console_tail = (console_tail + 1) % sizeof(console_ring);
        console_dropped++;
    }
    console_ring[console_head] = ch ? ch : '?';
    console_head = next;
    k_spin_unlock(&console_lock, key);
}

static bool command_allowed(const char *cmd)
{
    static const char *const commands[] = {
        "xu list", "xu config_list",
        "xu create rpi_5_domu", "xu create rpi_5_domu -p",
        "xu create rtt_domu", "xu create rtt_domu -p",
        "xu destroy rpi_5_domu", "xu destroy rtt_domu",
        "xu pause rpi_5_domu", "xu pause rtt_domu",
        "xu unpause rpi_5_domu", "xu unpause rtt_domu",
    };
    for (size_t i = 0; i < ARRAY_SIZE(commands); i++)
        if (!strcmp(cmd, commands[i])) return true;
    return false;
}

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int upload_end(void)
{
    if (!uploading || received_size != expected_size) return -EINVAL;
    if (find_domain_by_name("rtt_domu")) return -EBUSY;
    int rc = fs_sync(&upload);
    int close_rc = fs_close(&upload);
    uploading = false;
    if (rc || close_rc) return rc ? rc : close_rc;
    if (received_crc != expected_crc) return -EBADMSG;
    /* Verify the SD file before replacing the previously bootable image. */
    fs_file_t_init(&upload);
    rc = fs_open(&upload, DISK_BIN_PATH "rtt.new", FS_O_READ);
    if (rc) return rc;
    uint8_t data[512];
    uint32_t crc = 0;
    size_t size = 0;
    ssize_t count;
    while ((count = fs_read(&upload, data, sizeof(data))) > 0) {
        crc = crc32_ieee_update(crc, data, count);
        size += count;
    }
    fs_close(&upload);
    if (count < 0) return count;
    if (size != expected_size || crc != expected_crc) return -EBADMSG;
    fs_unlink(DISK_BIN_PATH "rtt.bak");
    rc = fs_rename(DISK_BIN_PATH "rtt.bin", DISK_BIN_PATH "rtt.bak");
    if (rc && rc != -ENOENT) return rc;
    rc = fs_rename(DISK_BIN_PATH "rtt.new", DISK_BIN_PATH "rtt.bin");
    if (rc) {
        fs_rename(DISK_BIN_PATH "rtt.bak", DISK_BIN_PATH "rtt.bin");
        return rc;
    }
    snprintf(output, sizeof(output), "%zu %08x", size, crc);
    return 0;
}

static int dispatch(char *verb)
{
    const struct shell *sh = shell_backend_dummy_get_ptr();
    output[0] = 0;
    if (!strncmp(verb, "CMD ", 4)) {
        if (!command_allowed(verb + 4)) return -EPERM;
        if (uploading && !strncmp(verb + 4, "xu create rtt_domu", 18)) return -EBUSY;
        shell_backend_dummy_clear_output(sh);
        bool rtt_create = !strncmp(verb + 4, "xu create rtt_domu", 18);
        bool sync_create = !strncmp(verb + 4, "xu create rpi_5_domu", 20);
        int rc = shell_execute_cmd(sh, rtt_create ? "xu create rtt_domu -p" :
                                       sync_create ? "xu create rpi_5_domu -p" : verb + 4);
        if (!rc && (rtt_create || sync_create)) {
            uint32_t id = find_domain_by_name(rtt_create ? "rtt_domu" : "rpi_5_domu");
            rc = id ? pin_vcpu(id, 0, rtt_create ? 8 : 7) : -ESRCH;
            if (rtt_create) rtt_pin_rc = rc;
            if (!rc && !strstr(verb + 4, " -p")) rc = xen_domctl_unpausedomain(id);
        }
        size_t size;
        const char *text = shell_backend_dummy_get_output(sh, &size);
        size = MIN(size, sizeof(output) - 1);
        memcpy(output, text, size);
        output[size] = 0;
        return rc;
    }
    if (!strncmp(verb, "ATTACH ", 7)) {
        char *end;
        unsigned long id = strtoul(verb + 7, &end, 10);
        if (*end || id < 2 || id > 32751) return -EINVAL;
        struct xen_domain *domain = get_domain(id);
        if (!domain) return -ESRCH;
        /* Only the two named test domains may feed this channel. */
        uint32_t rtt = find_domain_by_name("rtt_domu");
        uint32_t sync = find_domain_by_name("rpi_5_domu");
        int rc = (id == rtt || id == sync) ?
                 set_console_feed_cb(domain, console_feed, NULL) : -EPERM;
        put_domain(domain);
        return rc;
    }
    if (!strcmp(verb, "LOG")) {
        size_t size = 0;
        k_spinlock_key_t key = k_spin_lock(&console_lock);
        while (console_tail != console_head && size < sizeof(output) - 1) {
            output[size++] = console_ring[console_tail];
            console_tail = (console_tail + 1) % sizeof(console_ring);
        }
        output[size] = 0;
        k_spin_unlock(&console_lock, key);
        return 0;
    }
    if (!strcmp(verb, "STATUS")) {
        snprintf(output, sizeof(output), "remote-admin-v1 upload=%u bytes=%zu dropped=%u dom0_pin=%d linux_pin=%d rtt_pin=%d",
                 uploading, received_size, console_dropped, dom0_pin_rc, linux_pin_rc, rtt_pin_rc);
        return 0;
    }
    if (!strncmp(verb, "BEGIN ", 6)) {
        char *end;
        unsigned long size = strtoul(verb + 6, &end, 10);
        if (*end != ' ' || !size || size > 8U * 1024U * 1024U || uploading) return -EINVAL;
        unsigned long crc = strtoul(end + 1, &end, 16);
        if (*end) return -EINVAL;
        if (find_domain_by_name("rtt_domu")) return -EBUSY;
        fs_file_t_init(&upload);
        int rc = fs_unlink(DISK_BIN_PATH "rtt.new");
        if (rc && rc != -ENOENT) return rc;
        rc = fs_open(&upload, DISK_BIN_PATH "rtt.new", FS_O_CREATE | FS_O_WRITE);
        if (rc) return rc;
        expected_size = size; expected_crc = crc;
        received_size = 0; received_crc = 0; uploading = true;
        return 0;
    }
    if (!strcmp(verb, "ABORT")) {
        if (uploading) fs_close(&upload);
        uploading = false;
        fs_unlink(DISK_BIN_PATH "rtt.new");
        return 0;
    }
    if (!strncmp(verb, "DATA ", 5)) {
        uint8_t data[512];
        size_t len = strlen(verb + 5);
        if (!uploading || !len || len % 2 || len > sizeof(data) * 2) return -EINVAL;
        size_t size = len / 2;
        if (received_size + size > expected_size) return -EFBIG;
        for (size_t i = 0; i < size; i++) {
            int high = hex_digit(verb[5 + i * 2]);
            int low = hex_digit(verb[6 + i * 2]);
            if (high < 0 || low < 0) return -EINVAL;
            data[i] = (high << 4) | low;
        }
        ssize_t count = fs_write(&upload, data, size);
        if (count != (ssize_t)size) return count < 0 ? count : -EIO;
        received_crc = crc32_ieee_update(received_crc, data, size);
        received_size += size;
        return 0;
    }
    if (!strcmp(verb, "END")) return upload_end();
    return -ENOTSUP;
}

static void admin_main(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    char last_token[24] = "";
    dom0_pin_rc = pin_vcpu(0, 0, 7);
    linux_pin_rc = 0;
    for (unsigned cpu = 0; cpu < 3; cpu++) {
        int rc = pin_vcpu(1, cpu, 7);
        if (rc) linux_pin_rc = rc;
    }
    xss_write_guest_domain_rw(BASE "request", "", 1);
    xss_write_guest_domain_ro(BASE "response", "ready", 1);
    xss_write_guest_domain_ro(BASE "version", "1", 1);
    for (;;) {
        if (!xss_read(BASE "request", request, sizeof(request)) && request[0]) {
            request[sizeof(request) - 1] = 0;
            char *space = strchr(request, ' ');
            if (space && (size_t)(space - request) < sizeof(last_token)) {
                *space = 0;
                if (strcmp(request, last_token)) {
                    strcpy(last_token, request);
                    int rc = dispatch(space + 1);
                    snprintf(response, sizeof(response), "%s %d\n%s", last_token, rc, output);
                    xss_write_guest_domain_ro(BASE "response", response, 1);
                }
            }
        }
        k_sleep(K_MSEC(10));
    }
}

void remote_admin_start(void)
{
    k_thread_create(&admin_thread, admin_stack, K_THREAD_STACK_SIZEOF(admin_stack),
                    admin_main, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
    k_thread_name_set(&admin_thread, "remote-admin");
}
