# Third-party hosts: the observations behind each family

Every name below came off a console or an emulator on a dated run — none was guessed.
Each is reached directly by a title, with no Nintendo host in the path, and its family is
now claimed: a console on OpenPak sends it to OpenPak.

OpenPak runs a replacement for none of them yet. They are claimed anyway: each service
asks Nintendo to vouch for a token an OpenPak console does not have, so the connection was
going to be refused. Failing to connect and failing to authenticate cost the player the
same thing, and this way the console never talks to them.

This file is the evidence for adding those families, and the place to record what a
title turns out to need once something of ours does answer.

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
| `3f284.playfabapi.com` | We Were Here — Microsoft PlayFab (title `3F284`) | 2026-10-10 | Multiplayer sweep: resolved to a Microsoft address and opened TLS to it. Family `.playfabapi.com` is claimed from signed ceiling v5 on; nx-baas answers every call with PlayFab's own ServiceUnavailable (503, errorCode 1123) until a PlayFab service exists. Minecraft Dungeons also uses PlayFab. |

## Serving one

Build the replacement and route it; the family is already claimed, so no client change
is needed. A family that is not claimed yet goes into `website/internal/netprofile/
families.go` (the ceiling) and `platforms.yml` (the Switch's families) together, then a
publish. Never into `builtin_rules` in `source/hosts.c`: that list is the frozen last
resort for a console that cannot read a bundle, and it mirrors the published bundle.
