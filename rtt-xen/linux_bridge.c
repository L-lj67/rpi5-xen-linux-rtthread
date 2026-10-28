/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "control_shared.h"
typedef uint32_t grant_ref_t;
typedef uint16_t domid_t;
#include <xen/gntdev.h>

static volatile sig_atomic_t stopping;
static void stop(int signal) { (void)signal; stopping = 1; }
static uint64_t now_ns(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}
static void sleep_ms(unsigned ms)
{
    struct timespec time = { ms / 1000, (ms % 1000) * 1000000UL };
    while (nanosleep(&time, &time) && errno == EINTR && !stopping) {}
}
static void publish(struct rtt_control_shared *shared, int x, int y, unsigned command)
{
    struct rtt_control_input *input = &shared->input;
    uint32_t sequence = (__atomic_load_n(&input->sequence, __ATOMIC_ACQUIRE) + 2) & ~1U;
    __atomic_store_n(&input->sequence, sequence - 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    input->command = command;
    input->target_x = x;
    input->target_y = y;
    input->external_mode = 1;
    input->sent_counter = now_ns();
    __atomic_store_n(&input->sequence, sequence, __ATOMIC_RELEASE);
}
static int snapshot(struct rtt_control_shared *shared, struct rtt_control_output *out)
{
    for (unsigned i = 0; i < 100; i++) {
        uint32_t sequence = __atomic_load_n(&shared->output.sequence, __ATOMIC_ACQUIRE);
        if (sequence & 1) continue;
        memcpy(out, &shared->output, sizeof(*out));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (sequence == __atomic_load_n(&shared->output.sequence, __ATOMIC_ACQUIRE)) return 0;
    }
    return -1;
}
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 5) {
        fprintf(stderr, "usage: %s DOMID [--demo SECONDS | --set X Y | --snapshot]\n", argv[0]);
        return 2;
    }
    char *end;
    unsigned long domid = strtoul(argv[1], &end, 10);
    if (*end || domid < 2 || domid > 32751) return 2;
    int demo = argc >= 3 && !strcmp(argv[2], "--demo");
    int set = argc >= 3 && !strcmp(argv[2], "--set");
    if ((demo && argc != 4) || (set && argc != 5)) return 2;
    unsigned seconds = demo ? strtoul(argv[3], NULL, 10) : 0;
    if (seconds > 3600) return 2;
    signal(SIGINT, stop); signal(SIGTERM, stop);
    int fd = open("/dev/xen/gntdev", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open gntdev"); return 1; }
    /* Control ABI fits in the first Xen 4KiB grant. gntdev allocates one
     * Linux page per grant, including with this kernel's 16KiB pages. */
    const unsigned map_count = 1;
    size_t mapping_size = (size_t)sysconf(_SC_PAGESIZE);
    size_t request_size = sizeof(struct ioctl_gntdev_map_grant_ref);
    struct ioctl_gntdev_map_grant_ref *map = calloc(1, request_size);
    if (!map) return 1;
    map->count = map_count;
    for (unsigned i = 0; i < map_count; i++) {
        map->refs[i].domid = domid;
        map->refs[i].ref = RTT_GRANT_FIRST + i;
    }
    if (ioctl(fd, IOCTL_GNTDEV_MAP_GRANT_REF, map)) { perror("map grants"); return 1; }
    struct rtt_control_shared *shared = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                                           MAP_SHARED, fd, map->index);
    if (shared == MAP_FAILED) { perror("mmap grants"); return 1; }
    int result = 0;
    if (shared->magic != RTT_CONTROL_MAGIC || shared->abi != RTT_CONTROL_ABI || shared->size != RTT_SHARED_SIZE) {
        fprintf(stderr, "RTT shared memory ABI mismatch\n"); result = 1; goto cleanup;
    }
    uint64_t until = now_ns() + (uint64_t)seconds * 1000000000;
    unsigned command = 0;
    do {
        if (demo) publish(shared, ((command / 40) & 1) ? 1000 : -1000, 0, command);
        else if (set) publish(shared, atoi(argv[3]), atoi(argv[4]), command);
        if (demo || set) sleep_ms(15);
        struct rtt_control_output out;
        if (!snapshot(shared, &out)) {
            printf("{\"control\":%llu,\"input_seq\":%u,\"pan\":%d,\"tilt\":%d,\"compute_us\":%llu,\"max_compute_us\":%llu,\"max_late_us\":%llu,\"deadline_misses\":%u,\"fp_failures\":%u}\n",
                   (unsigned long long)out.control_count, out.input_sequence, out.pan, out.tilt,
                   (unsigned long long)out.compute_us, (unsigned long long)out.max_compute_us,
                   (unsigned long long)out.max_late_us, out.deadline_misses, out.fp_failures);
            fflush(stdout);
            if (out.fp_failures) result = 1;
        }
        command++;
        if (demo) sleep_ms(35);
    } while (demo && !stopping && now_ns() < until);
cleanup:
    munmap(shared, mapping_size);
    struct ioctl_gntdev_unmap_grant_ref unmap = { .index = map->index, .count = map_count };
    if (ioctl(fd, IOCTL_GNTDEV_UNMAP_GRANT_REF, &unmap)) { perror("unmap grants"); result = 1; }
    close(fd); free(map);
    return result;
}
