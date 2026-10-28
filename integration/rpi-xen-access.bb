SUMMARY = "Public-key SSH and direct Ethernet access for the Xen development image"
LICENSE = "CLOSED"

SRC_URI = "file://10-rpi-direct.network file://10-rpi-xen.conf"
RDEPENDS:${PN} = "openssh-sshd openssh-keygen openssh-sftp-server"

# Set these in your private Yocto local.conf. Never commit a private key.
RPI_XEN_SSH_PUBKEY ?= ""
RPI_XEN_STATIC_ADDR ?= "192.168.77.2/24"

python do_install() {
    import base64
    import ipaddress
    import os
    from pathlib import Path
    import shutil

    key = d.getVar('RPI_XEN_SSH_PUBKEY').strip()
    fields = key.split()
    if len(fields) < 2 or fields[0] not in ('ssh-ed25519', 'ssh-rsa', 'ecdsa-sha2-nistp256', 'ecdsa-sha2-nistp384', 'ecdsa-sha2-nistp521') or '\n' in key or '\r' in key:
        bb.fatal('Set RPI_XEN_SSH_PUBKEY to one valid SSH public key in private local.conf')
    try:
        base64.b64decode(fields[1], validate=True)
        address = str(ipaddress.IPv4Interface(d.getVar('RPI_XEN_STATIC_ADDR')))
    except ValueError:
        bb.fatal('Invalid SSH public-key encoding or RPI_XEN_STATIC_ADDR')
    config = Path(d.getVar('D') + d.getVar('sysconfdir'))
    work = Path(d.getVar('WORKDIR'))
    for relative in ('ssh/authorized_keys', 'ssh/sshd_config.d', 'systemd/network'):
        (config / relative).mkdir(parents=True, exist_ok=True)
    target = config / 'ssh/authorized_keys/root'
    target.write_text(' '.join(fields[:2]) + '\n')
    os.chmod(target, 0o600)
    shutil.copyfile(work / '10-rpi-xen.conf', config / 'ssh/sshd_config.d/10-rpi-xen.conf')
    network = (work / '10-rpi-direct.network').read_text().replace('@ADDRESS@', address)
    (config / 'systemd/network/10-rpi-direct.network').write_text(network)
}

FILES:${PN} = "${sysconfdir}/ssh ${sysconfdir}/systemd/network/10-rpi-direct.network"
