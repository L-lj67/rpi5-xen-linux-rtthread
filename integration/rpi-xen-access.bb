SUMMARY = "Public-key SSH and direct Ethernet access for the Xen development image"
LICENSE = "CLOSED"

SRC_URI = "file://10-rpi-direct.network file://10-rpi-xen.conf"
RDEPENDS:${PN} = "openssh-sshd openssh-keygen openssh-sftp-server"

# Set these in your private Yocto local.conf. Never commit a private key.
RPI_XEN_SSH_PUBKEY ?= ""
RPI_XEN_STATIC_ADDR ?= "192.168.77.2/24"

do_install() {
    test -n "${RPI_XEN_SSH_PUBKEY}" || bbfatal "Set RPI_XEN_SSH_PUBKEY to your own public key in local.conf"
    install -d -m 0755 ${D}${sysconfdir}/ssh/authorized_keys
    printf '%s\n' "${RPI_XEN_SSH_PUBKEY}" > ${D}${sysconfdir}/ssh/authorized_keys/root
    chmod 0600 ${D}${sysconfdir}/ssh/authorized_keys/root
    install -d -m 0755 ${D}${sysconfdir}/ssh/sshd_config.d
    install -m 0644 ${WORKDIR}/10-rpi-xen.conf ${D}${sysconfdir}/ssh/sshd_config.d/10-rpi-xen.conf
    install -d -m 0755 ${D}${sysconfdir}/systemd/network
    sed "s|@ADDRESS@|${RPI_XEN_STATIC_ADDR}|g" ${WORKDIR}/10-rpi-direct.network > ${D}${sysconfdir}/systemd/network/10-rpi-direct.network
    chmod 0644 ${D}${sysconfdir}/systemd/network/10-rpi-direct.network
}

FILES:${PN} = "${sysconfdir}/ssh ${sysconfdir}/systemd/network/10-rpi-direct.network"
