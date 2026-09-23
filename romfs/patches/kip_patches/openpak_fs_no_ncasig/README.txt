Verified FS NCA-header-signature kip patch goes here.

This directory is the install payload for the "Store installs" step. It is
EMPTY of .ips on purpose: no hardware-verified FS patch exists yet, so the
step reports "Pending" and installs nothing.

When a patch is verified on hardware (see docs/install-trust.md and the
candidate under docs/install-trust/), drop the IPS here named by the FS KIP
hash Atmosphere matches, rebuild, and the step will install it to
/atmosphere/kip_patches/openpak_fs_no_ncasig/ .

The .ips filename must gate on this console's FS KIP so it can never apply to
another firmware. FS 22.5.0 KIP sha256:
536d938469fe73be3c76da0333b289c0ed29f10c2a8afdff8e466142c4277359
