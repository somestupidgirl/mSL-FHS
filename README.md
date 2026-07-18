# mSL/XNU

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-macOS-lightgrey)](#requirements)

**macOS Subsystem for Linux / X is Now UNIX** — a filesystem-layout compatibility
layer that presents macOS through a Linux-shaped namespace.

## What is mSL/XNU?

macOS is a certified UNIX, and nearly everything a Linux program expects from the
filesystem is already present on the machine — it is simply in a different place,
under a different name, or hidden from view. Home directories live in `/Users`
rather than `/home`. Mounted volumes appear in `/Volumes` rather than `/media`.
`/proc` and `/sys` do not exist at all.

mSL/XNU closes that gap at the namespace level. It is **not** a container, an
emulator, or a virtual machine: there is no second kernel and no translation
layer. The processes are macOS processes, the filesystems are macOS filesystems,
and the kernel is XNU. Only the *shape* of the namespace changes, so that a
program (or a person) that expects the Filesystem Hierarchy Standard finds what
it is looking for.

The project is deliberately split into two halves that can be used
independently:

- **The layout layer** (this repository) — the static shape of the tree. Home
  directories, mount points, removable media. Built out of symlinks maintained
  by a small daemon, with no kernel code at all.
- **The pseudo-filesystems** (separate projects) — the synthetic, dynamic parts
  of a Linux tree, which have to be real filesystems because their contents are
  generated on demand:
  [procfs](https://github.com/somestupidgirl/procfs_kext) for `/proc`, and a
  planned `sysfs` for `/sys`.

mSL/XNU **detects and reports** the status of the pseudo-filesystems, but never
mounts, unmounts, or configures them — each keeps its own installer and its own
toggles. This avoids two applications fighting over the same mount point and
reporting state that contradicts reality.

Tested on:

    - macOS 26.5.2 (Tahoe), Darwin 25.5.0, Apple Silicon (arm64) — primary target

## Design

### Two tiers

The single most important constraint on macOS is that **the root directory is not
writable.** The system volume is sealed (APFS Signed System Volume) and mounted
read-only; System Integrity Protection prevents modification even as root. A
directory cannot simply be created at `/`.

The only supported mechanism for adding entries to `/` is `/etc/synthetic.conf`,
which is parsed exactly once per boot, early, by `apfs_boot_util`. Nothing can
make it take effect again without a restart. That single fact splits the layer in
two:

| Tier | What it is | When it changes | In the toggle? |
|------|------------|-----------------|----------------|
| **Skeleton** | The root-level entries themselves (`/mnt`, `/media`, …), created as symlinks into the writable Data volume via `synthetic.conf` | Install time only; **requires a reboot** | No |
| **Contents** | What lives inside them — the per-user and per-volume symlinks, maintained by `mslxd` | Continuously, at runtime | Yes |

The skeleton is inert. A `/media` symlink pointing at an empty directory is
indistinguishable, to any program, from a system that never had `/media` — so
leaving it in place while the layer is switched off costs nothing. All the
observable behaviour lives in the contents tier, which is fully dynamic.

The practical consequence: **installing mSL/XNU requires one reboot, and so does
adding a new root-level directory later.** Everything else — enabling a
component, disabling it, uninstalling the layer's effects — happens live. This is
a real limitation of the platform rather than of the implementation, and the
installer states it plainly.

### Symlink farms, not mounts

Every component is a directory on the writable Data volume, reached through a
symlink at `/`, whose contents are symlinks maintained by the daemon:

```
/media -> /System/Volumes/Data/media
          └── sunneva/
              └── USB_STICK -> /Volumes/USB STICK
```

This was chosen over the more obvious alternatives:

- **autofs maps** can be reloaded live (`automount -c`) and are the right tool
  for `/home`, where macOS already uses them — but they mount *filesystems*, and
  macOS has no loopback/bind filesystem to redirect one existing path to
  another. They also enumerate poorly, so `ls /media/$USER` would not reliably
  list what is mounted.
- **A kernel extension** could hook `lookup` and synthesise these paths, but on
  Apple Silicon that costs Reduced Security, a user-approved load, an auxiliary
  kernel collection rebuild, and a reboot — an enormous price for a symlink, on
  a mechanism Apple continues to narrow. The pseudo-filesystems justify that
  cost because they genuinely must synthesise content in the kernel. This layer
  does not.

Symlinks are boring, inspectable with `ls -l`, removable without privilege
escalation, and leave nothing behind when deleted.

### Components

Each is independently toggleable. See [docs/LAYOUT.md](docs/LAYOUT.md) for the
full per-directory semantics.

| Component | Provides | Populated by | Notes |
|-----------|----------|--------------|-------|
| `home` | `/home/<user>` → `/Users/<user>` | One symlink per local user | Requires disabling the `auto_home` automounter map |
| `mnt` | `/mnt` | Nothing — stays empty | Correct: `/mnt` is empty on a stock Linux system |
| `media` | `/media/<user>/<label>` → `/Volumes/<label>` | DiskArbitration events, live | Removable media only; filtered and user-attributed |

`/mnt` being empty is not an unimplemented feature. The FHS defines it as scratch
space for *temporary, manual* mounts (`mount /dev/sdb1 /mnt/usb`); nothing
populates it automatically on Linux, and mapping it onto `/Volumes` would produce
a layout no Linux system actually has.

`/media` is the auto-populated one, and it is the component that needs real work.
macOS puts *everything* in `/Volumes` — internal volumes, network shares, mounted
disk images, and removable devices, flat and without user attribution — whereas
Linux's `/media/<user>/<label>` contains only removable media mounted by that
user's session. Reproducing it faithfully means filtering by device class and
matching udisks2's naming rules, which is what `mslxd` does.

### Components at runtime

`mslxd` is a root LaunchDaemon (`KeepAlive`), mirroring the `procfsd` design:

- Reconciles every enabled component at startup — the boot-time restore
- Subscribes to DiskArbitration and keeps `/media` in step as volumes appear
  and disappear
- Follows the console user, so a login or fast user switch re-attributes
  `/media`
- Polls slowly (60s) for account changes, which nothing notifies on
- Will serve state queries and toggle requests to the GUI

It holds **no state of its own**. Every wakeup re-reads the system and
reconciles, so a missed event is corrected by the next one rather than leaving
the daemon permanently out of step, and `KeepAlive` restarting it is always
safe. It also calls the same sync functions `mslctl` does, so the two can never
disagree about what should exist.

Mounting one volume produces several DiskArbitration callbacks — appeared, then
description changes as the mount completes. Each schedules a single
short-delay timer rather than reconciling immediately, so a burst collapses
into one pass and the volume is never observed mid-mount before its path is
set.

Privileged actions go through the standard macOS administrator-authorization
prompt, as procfs's companion app does. With a per-component matrix that would
mean a prompt per checkbox, so the preference pane **batches**: pending changes
are collected and applied in one authorized step.

Routing mutations through the daemon instead — it is already root, so it could
apply them without a prompt — was considered and rejected. It would mean a root
process accepting reconfiguration commands over a socket, and any peer policy
permissive enough to be convenient would also let anything running as the user
silently remask `/etc/auto_master`. The prompt is not only an access check: it
is what makes a change to the system visible and attributable to the person
making it. `mslxd`'s command surface stays closed.

## Interface

**Menu bar app** — live status for each component and the detected
pseudo-filesystems, one-click toggles, and quick access to preferences.

**Preference pane** (`/Library/PreferencePanes/mSL.prefPane`) — per-component
enable/disable, start-at-boot, and update settings.

Both read state live rather than caching it, so they never disagree with the
filesystem.

## Requirements

- macOS 15 or later (Apple Silicon or Intel)
- Administrator access for installation
- **One reboot** to install the skeleton

No kernel extension, no Reduced Security, and no SIP changes are required for the
layout layer. (The optional pseudo-filesystems have their own requirements.)

## Status

Early. The design is settled; the implementation is not yet written.

| Component | Status |
|-----------|--------|
| `/home` | **Working** — verified on macOS 26.5.2 (arm64) |
| `/mnt` | **Working** — needs one reboot to appear |
| `/media` | **Working** — needs one reboot to appear |
| `mslctl` | **Working** — CLI control for the layer |
| `mslxd` | **Built, not yet exercised** — boot restore and live volume tracking |
| Menu bar app | Planned |
| Preference pane | Planned |
| `/proc` detection | Planned ([procfs](https://github.com/somestupidgirl/procfs_kext) exists and is mature) |
| `/sys` detection | Planned (`sysfs` not yet started) |

### Not planned, and why

**Unhiding `/bin`, `/usr`, `/etc`, `/dev` and friends.** These directories all
already exist on macOS with their normal UNIX contents; they are merely hidden
from Finder by a filesystem flag on the sealed system volume, which SIP does not
permit clearing. This is a presentation issue in one application rather than a
namespace issue, and there is no supported way to change it. Any program,
shell, or script already sees these paths normally.

## License

MIT — see [LICENSE](LICENSE).
