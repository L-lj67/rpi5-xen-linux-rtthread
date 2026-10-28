# The pinned Poky pseudo predates the host-compatibility fixes used by this port.
# Keep upstream pseudo.inc/fallback files; use the tested pseudo 1.9.11 revision.
PV = "1.9.11"
SRCREV = "ba8887e5f1e922f866681ec7dec1a00b602a9328"
SRC_URI:remove = "file://0001-configure-Prune-PIE-flags.patch file://glibc238.patch file://older-glibc-symbols.patch"
