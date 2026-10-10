# Auxiliary-core Linux integration

The baseline cvitek_remoteproc.c comes from the [existing SG200x Debian driver patch](https://github.com/scpcom/sophgo-sg200x-debian/blob/6f102cbb1f490046603e5a458d81e244e00f4bbd/configs/chip/sg200x/patches/linux/0003-Add-Remoteproc-Driver-for-cvitek.patch).
Source pin: scpcom/sophgo-sg200x-debian@6f102cbb1f490046603e5a458d81e244e00f4bbd.
It retains the GPL-2.0-only / STMicroelectronics copyright header.

Add the upstream driver/integration to the matching kernel first, then apply this
repository's prepare.patch and wake.patch, in that order. Both patches passed
git apply --check --whitespace=error against that baseline during publication.
The preparation holds the auxiliary reset through ELF loading; wake exposes a
firmware-guarded mailbox kick. Neither patch belongs to the monitor's Apache license.

Linux remoteproc core configuration, device tree, reset/clock/mailbox providers
and the reserved RAM layout must match the prepared board. This directory holds
patches, not a replacement kernel or a ready-to-flash generic system image.
