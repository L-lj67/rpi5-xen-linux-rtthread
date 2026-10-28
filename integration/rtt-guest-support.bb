SUMMARY = "Linux bridge and startup service for the RT-Thread Xen guest"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=d229da563da18fe5d58cd95a6467d584"

SRC_URI = "file://LICENSE file://linux_bridge.c file://control_shared.h file://guest_service.py file://rtt-guest.service"
S = "${WORKDIR}"

inherit systemd
SYSTEMD_SERVICE:${PN} = "rtt-guest.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"
RDEPENDS:${PN} = "python3-core python3-json python3-fcntl python3-crypt python3-misc python3-shell"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -I${WORKDIR} ${WORKDIR}/linux_bridge.c -o ${B}/rtt-linux-bridge
}

do_install() {
    install -d -m 0700 ${D}/root/xen-tests
    install -m 0700 ${B}/rtt-linux-bridge ${D}/root/xen-tests/rtt-linux-bridge
    install -m 0600 ${WORKDIR}/guest_service.py ${D}/root/xen-tests/guest_service.py
    install -d -m 0755 ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/rtt-guest.service ${D}${systemd_system_unitdir}/rtt-guest.service
}

FILES:${PN} += "/root/xen-tests/rtt-linux-bridge /root/xen-tests/guest_service.py ${systemd_system_unitdir}/rtt-guest.service"
