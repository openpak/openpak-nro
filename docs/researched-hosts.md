# Hosts we researched but do not serve

These names came off a console or an emulator on a dated run — none was guessed. Each is
reached directly by a third party, outside every family OpenPak claims, and OpenPak runs no
replacement for any of them — so none is redirected, by this NRO or by the bundle the server
publishes.

A name *inside* a claimed family is a different case and is not listed here: the family
covers it whether or not anything of ours answers, which is the trade described in the
README. `prod.depot.battle.net` moved into that category when `.battle.net` became a family.

Until 0.3.x they shipped inside the hosts block, written commented out. That made the
block self-documenting and cost a little noise. Now that the block is generated from the
signed policy bundle, an inert entry would mean shipping data to every console that no
console acts on, so the inventory lives here instead — with the tool that would use it,
which was always the point.

## Why they are off, and why that is not caution

This is not "don't break a working title". OpenPak's audience is banned, jailbroken and
emulated consoles. Every third party that asks Nintendo to vouch for the console's token
(Epic certainly, Demonware probably) refuses an OpenPak console regardless: our identity
is not Nintendo's, and a banned console cannot obtain Nintendo's. For most of these titles
the online half is already gone before this tool runs.

Turning one on trades an authentication failure for a connection failure until something
of ours answers on the other side.

We also do not comment out anyone *else's* redirect for these names, the way the enable
path does for the hosts we serve. We only claim what we answer for.

## The inventory

| Host | Title / service | Observed | Note |
| --- | --- | --- | --- |
| `lavender-switch-auth3.prod.demonware.net` | Crash Team Racing Nitro-Fueled — Demonware auth | 2026-08-31 | Live at the time. Whether the service accepts an OpenPak-identified console is untested; if it does, the title needs nothing from us. |
| `lavender-switch-lobby.prod.demonware.net` | Crash Team Racing Nitro-Fueled — LSG lobby | 2026-08-31 | As above. |
| `api.epicgames.dev` | Epic Online Services — identity for Fall Guys, Among Us | | Epic verifies the Nintendo token upstream, so an OpenPak-minted one cannot pass and a stub cannot stand in. |
| `*.ea.com` | PvZ: Battle for Neighborville — EA GOS/Blaze + Nucleus | | Only `spring18.gosredirector.ea.com` and the signin hosts were seen; the full set is unknown, so this is the whole suffix — which is exactly why it must stay off. |
| `title.mgt.xboxlive.com` | Minecraft Dungeons — Microsoft/Mojang | 2026-08-31 | The confirmed inventory up to the sign-in screen. No Nintendo host is involved at all; this needs an xbox-live adapter, not a per-title server. |
| `sisu.xboxlive.com` | Minecraft Dungeons | 2026-08-31 | |
| `login.live.com` | Minecraft Dungeons | 2026-08-31 | |
| `launchercontent.mojang.com` | Minecraft Dungeons | 2026-08-31 | |
| `vortex.data.microsoft.com` | Minecraft Dungeons | 2026-08-31 | |

## Turning one on

Add the name to the routing this server runs and let the bundle pick it up — the platform
map in `website/internal/netprofile/platforms.yml` and the family ceiling in `families.go`.
Do not add it to `builtin_rules` in `source/hosts.c`: that list is the frozen last resort
for a console that cannot read a bundle at all, not a place to ship a new title.
