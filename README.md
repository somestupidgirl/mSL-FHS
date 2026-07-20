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
| **Skeleton** | The root-level entries themselves (`/home`, `/mnt`, `/media`, …), created as symlinks into the writable Data volume via `synthetic.conf` | Install time only; **requires a reboot** | No |
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
| `home` | `/home/<user>` → `/Users/<user>` | One symlink per local user | Also masks the `auto_home` automounter map |
| `mnt` | `/mnt` | Nothing — the administrator mounts into it | Reported, never mounted by mSL |
| `media` | `/media/<user>/<label>` → `/Volumes/<label>` | DiskArbitration events, live | Removable media only; filtered and user-attributed |

`/mnt` staying empty is not an unimplemented feature. The FHS defines it as
scratch space for *temporary, manual* mounts (`mkdir /mnt/disk1 && mount
/dev/… /mnt/disk1`); nothing populates it automatically on Linux, and mapping it
onto `/Volumes` would produce a layout no Linux system actually has. The
component does *report* what you have mounted there — read-only; it never mounts
or unmounts anything.

`/home` needs the automounter masked because, while the `auto_home` map is
active, autofs owns the directory and nothing can be created in it. That has a
consequence worth stating plainly: **the root-level `/home` is not a permanent
part of macOS** — autofs creates it at boot from that very map. Masking the map
therefore removes `/home` itself at the next boot, so the component declares its
own `/home` in `synthetic.conf` alongside `/mnt` and `/media`.

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
- **One reboot** before the root-level directories appear

No kernel extension, no Reduced Security, and no SIP changes are required for the
layout layer. (The optional pseudo-filesystems have their own requirements.)

## Installing

From a release disk image, open the `.pkg` and follow the installer. From
source:

```sh
make                # build everything into out/
sudo make install   # install and start the daemon
```

Either way, **nothing is switched on by the install.** Enabling a component
edits system configuration — every component adds an entry to
`/etc/synthetic.conf`, and `/home` additionally masks a line in
`/etc/auto_master` — and an installer making changes that affect login, on a
machine whose setup it has not inspected, would be taking a decision that
belongs to the person running it.

Turn components on in **System Settings → mSL/XNU**, or from the menu bar, or:

```sh
mslctl status       # what is on, and what state it is in
mslctl home check   # is /home safe to enable on this machine?
sudo mslctl home enable
```

All three appear after a restart: macOS creates root-level entries only at
startup. The symlinks beneath them are built immediately, so everything is in
place when the directories appear.

To show or hide the root-level directories in the Finder:

```sh
mslctl vis                    # every node, and whether it can be changed
sudo mslctl vis show opt
sudo mslctl vis hide opt
```

To build a distributable installer:

```sh
make dmg        # out/mSL-XNU-<version>.dmg, containing the .pkg and uninstaller
make distcheck  # clean build, then verify the payload carries every component
```

## Uninstalling

`Uninstall mSL.command` from the disk image, or `sudo make uninstall` from
source. Both switch every component off *before* removing anything — disabling
is what restores `/etc/auto_master` and drops the `synthetic.conf` entries, so
removing the tools first would strand the system with a masked automounter line
and no supported way to restore it. A pristine copy of `/etc/auto_master` is
kept at `/var/db/msl.auto_master.orig` regardless.

## Status

Working, and verified on macOS 26.5.2 (Tahoe), Darwin 25.5.0, Apple Silicon.

| Component | Status |
|-----------|--------|
| `/home` | **Working** — survives reboot via its own `synthetic.conf` entry |
| `/mnt` | **Working** — reports filesystems mounted under it |
| `/media` | **Working** — tracks volumes live through DiskArbitration |
| Finder visibility | **Working** — for the nodes macOS permits; see below |
| `mslctl` | **Working** — CLI control for the layer |
| `mslxd` | **Working** — boot restore, live volume tracking, console-user changes |
| Menu bar app | **Working** — per-node dropdowns |
| Preference pane | **Working** — batched Apply |
| Installer | **Working** — `make dmg`, with an uninstaller |
| `/proc` detection | **Working** ([procfs](https://github.com/somestupidgirl/procfs_kext) exists and is mature) |
| `/sys` detection | **Working** — reports "not installed" until `sysfs` exists |

### Finder visibility

macOS hides most root-level directories from the Finder with the `UF_HIDDEN`
file flag. mSL/XNU can clear it — but only where the platform permits, and which
those are is not apparent from the path. Measured, not assumed:

| Nodes | Result |
|-------|--------|
| `/opt` `/cores` `/Volumes` | **Changeable** — firmlinked to the writable Data volume |
| `/bin` `/etc` `/sbin` `/tmp` `/usr` `/var` | `SF_RESTRICTED` — refused by SIP |
| `/home` and other root symlinks | Read-only — the entry itself is on the sealed root |
| `/private` | Refused, for a reason nothing exposes |
| `/dev` | Blocked three ways — see below |

An earlier version of this file claimed root-directory unhiding was uniformly
impossible under SIP. That was wrong: three nodes are genuinely changeable, and
clearing `/opt`'s flag makes it *fully* visible in the Finder, not merely dimmed.

The GUI shows a locked node's toggle disabled with the reason, rather than
offering a control that cannot work. Every change is verified by re-reading the
flag afterwards, because one filesystem accepts the call and ignores it.

**`/dev` cannot be shown in the Finder by any supported means.** All three
routes were tried:

- devfs accepts `chflags` and silently ignores it, so `UF_HIDDEN` cannot be cleared
- devfs does not implement `MNT_UPDATE`, so `mount -u` cannot clear `nobrowse`
- SIP's rootless protection refuses a second devfs stacked over `/dev`, `EPERM`
  even as root

That leaves a kernel extension as the only path, for one cosmetic directory —
so `/dev` visibility is deferred alongside the `sysfs` work rather than
attempted here.

### Not planned, and why

**Unhiding the SIP-protected and sealed directories.** `/bin`, `/etc`, `/usr`,
`/var` and friends live on the Signed System Volume, whose seal is a
cryptographic hash tree verified at boot. The `hidden` flag on those entries is
part of the sealed content: there is no writable inode to change, from userspace
or from a kernel extension, and altering it would break the seal and prevent the
machine from booting. Only a VFS-interception layer that *synthesises* a
rewritten view of `/` could do it — a different and much larger undertaking, and
one that belongs with the pseudo-filesystem work rather than here.

Any program, shell, or script already sees these paths normally; this affects
one application's presentation.

## License

MIT — see [LICENSE](LICENSE).
