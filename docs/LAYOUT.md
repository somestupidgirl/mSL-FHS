# Filesystem layout

The per-directory specification for the mSL/XNU layout layer: what each path
means on Linux, what backs it on macOS, how it is produced, and what it costs to
switch on and off.

The guiding rule throughout is **fidelity to what a Linux system actually does**,
not to what the layout superficially resembles. Where the two conflict, the
former wins — including when that means a directory stays empty.

## Summary

| Path | Linux semantics | macOS source | Mechanism | Live toggle | Status |
|------|-----------------|--------------|-----------|-------------|--------|
| `/home/<user>` | Per-user home directories | `/Users/<user>` | Symlink farm; `auto_home` disabled | Yes | **Working** |
| `/mnt` | Empty; scratch for manual mounts | — | Skeleton symlink only | Yes | Planned |
| `/media/<user>/<label>` | Removable media, per session | `/Volumes/<label>`, filtered | Symlink farm from DiskArbitration | Yes | Planned |
| `/proc` | Process pseudo-filesystem | — | External kext; **detected only** | n/a | Planned (detection) |
| `/sys` | Kernel object pseudo-filesystem | — | External; **detected only** | n/a | Not started |
| `/root` | Superuser home | `/var/root` | Skeleton symlink | No (skeleton) | Future |
| `/run` | Runtime state | `/var/run` | Skeleton symlink | No (skeleton) | Future |
| `/srv` | Service data | — | Skeleton empty symlink target | No (skeleton) | Future |
| `/opt` | Optional packages | `/opt` | **Already exists** | — | Nothing to do |
| `/bin` `/sbin` `/usr` `/etc` `/dev` `/tmp` `/var` | Standard UNIX | Themselves | **Already correct** | — | Nothing to do |

"Live toggle" refers to the *contents* tier. Every skeleton entry — the symlink
at `/` itself — is created by `/etc/synthetic.conf` at boot and cannot be added
or removed without a restart. See [Skeleton](#skeleton) below.

## Skeleton

`/etc/synthetic.conf` is the only supported way to add entries to a macOS root
directory. It is read once per boot by `apfs_boot_util`, before the system is
usable, and cannot be re-applied at runtime.

Its two forms behave very differently, and only one is useful here:

```
name                    # creates an EMPTY DIRECTORY in the read-only
                        # synthesized root — cannot be written into,
                        # usable only as a mount point

name<TAB>/abs/target    # creates a SYMLINK to an absolute path
```

Because the first form is read-only, mSL/XNU uses the second exclusively,
pointing each entry at the writable Data volume:

```
mnt	/System/Volumes/Data/mnt
media	/System/Volumes/Data/media
```

This is precisely the pattern macOS itself uses for `/home`, which ships as a
symlink to `/System/Volumes/Data/home`. The targets are ordinary writable
directories, so the daemon can maintain their contents with nothing more than
`symlink(2)` and `unlink(2)`.

Constraints worth knowing:

- An entry **cannot shadow a path that already exists** at `/`. This is why
  `/home` needs no skeleton entry (macOS provides it) and `/opt` needs none
  either (it already exists on the system).
- The target of a symlink entry is not created for you. The installer must
  create the Data-volume directories before the reboot that activates them.
- The file is consumed at boot only. There is no daemon, no reload, and no
  supported override; a new root-level directory always costs a restart.

## `/home`

**Linux:** `/home/<username>` is the user's home directory, and `~` expands to
it. Universal on Linux; effectively never `/Users`.

**macOS:** home directories are `/Users/<username>`. `/home` exists but is a
symlink to `/System/Volumes/Data/home`, which is an **autofs mount point** — the
`auto_home` map, declared in `/etc/auto_master`:

```
/home			auto_home	-nobrowse,hidefromfinder
```

On a stock system this map resolves nothing locally; it exists to support
NFS-mounted network homes. While it is active it also *owns* the directory, so
symlinks cannot be created there.

**Mechanism.** Disable the map, then maintain a symlink farm in the directory it
was occupying:

1. Comment out the `/home` line in `/etc/auto_master` (backing up the original)
2. `automount -vc` to flush the automounter, unmounting `/home`
3. Create `/System/Volumes/Data/home/<user>` → `/Users/<user>` per local user

`/System/Volumes/Data/home` is on the writable Data volume, so step 3 needs no
special privilege beyond root, and the existing `/home` symlink means no
skeleton entry is required. **`/home` is therefore the one component that needs
no reboot at all**, which is why it is the first implementation target.

**Which users.** Local, human accounts only — those with a real home under
`/Users`, a valid login shell, and a UID at or above 500. macOS carries a large
number of system service accounts (`_windowserver`, `_spotlight`, …) with homes
like `/var/empty`, none of which belong in `/home`. The `/Users/Shared`
directory is not a user account and is excluded.

**Staying current.** Accounts are created and deleted while the system runs, so
the daemon reconciles the farm on account changes rather than only at boot.

**Trade-off to be explicit about.** This edits `/etc/auto_master`, a system
configuration file, and disables a macOS feature. On a machine using NFS network
home directories it would break them. The daemon must detect that case and
refuse rather than proceed, and uninstall must restore the original file exactly.

### Verified behaviour

Confirmed on macOS 26.5.2, Apple Silicon:

- **`automount -vc` releases the mount synchronously.** It reports
  `/System/Volumes/Data/home unmounted` and the directory is immediately
  writable, so enable completes in one pass with no reboot. The implementation
  still detects and reports the case where the mount is not released, rather
  than emitting a symlink error per account.
- **Paths resolve, and writes land in the real home.** `/home/<user>/…` opens
  the same files as `/Users/<user>/…`, as it must, being one directory.

Two consequences of using symlinks rather than a bind mount, both expected and
neither worth engineering around:

- **`$HOME` remains `/Users/<user>`**, and `~` expands to it. The layer changes
  what paths *exist*, not what the system believes about itself. Rewriting
  `$HOME` would be considerably more invasive, would diverge from what macOS
  applications expect, and would gain nothing — both paths reach the same
  directory.
- **Canonicalisation resolves through the link.** `pwd -P` inside
  `/home/<user>` reports `/Users/<user>`, and any program calling
  `realpath(3)` sees the macOS path. A program that canonicalises a path and
  then string-matches it against `/home` will not match. This is intrinsic to
  symlinks; only a real bind mount would behave otherwise, and macOS provides
  none.

## `/mnt`

**Linux:** the FHS defines `/mnt` as a mount point for a *temporarily mounted*
filesystem — scratch space an administrator uses by hand:

```
mkdir /mnt/usb && mount /dev/sdb1 /mnt/usb
```

Nothing populates it automatically. **On a stock Linux system, `/mnt` is
empty.**

**macOS:** no equivalent, and none needed.

**Mechanism.** A skeleton symlink to an empty writable directory. That is the
whole component.

It is worth stating clearly, because the opposite is the intuitive guess:
**mapping `/mnt` onto `/Volumes` would be wrong.** It would produce a directory
full of automatically-appearing mounted volumes, which is not what `/mnt` is on
any Linux system, and would additionally duplicate `/media`'s job while getting
`/media`'s semantics wrong. An empty `/mnt` is the faithful result, and it makes
manual `mount` commands copied from Linux documentation work as written.

## `/media`

The component that requires real work.

**Linux:** `/media/<username>/<volume-label>`, created when *that user's
session* mounts *removable* media and removed on eject. This is the udisks2
convention that GNOME and other desktops drive. Older systems used a flat
`/media/<label>`; mSL/XNU targets the per-user form, which is what current Linux
desktops produce and which is the only form that makes sense with more than one
user logged in.

Notably, `/media` contains **only removable media**. Internal filesystems mount
wherever `/etc/fstab` says — `/`, `/boot`, `/home`, and so on — and never appear
here. Network shares likewise do not.

**macOS:** everything mounts in `/Volumes`, flat, with no user attribution:
internal APFS volumes, external drives, mounted disk images, SMB and AFP network
shares. The boot volume appears as a *symlink*, not a mount:

```
/Volumes/Macintosh HD -> /
```

A naive `/media` → `/Volumes` mapping would therefore present the boot volume,
every mounted `.dmg`, and every network share as removable media in the current
user's session — wrong on device class, wrong on ownership, and wrong on the
boot volume not being a mount at all.

**Mechanism.** `mslxd` subscribes to DiskArbitration and maintains the farm as
volumes come and go:

- `DARegisterDiskAppearedCallback` / `DARegisterDiskDisappearedCallback` for
  mount and unmount
- `DARegisterDiskDescriptionChangedCallback` for renames and late mounts

Each volume is admitted only if it passes the filter, then symlinked from
`/media/<user>/<sanitised-label>` to its real `/Volumes` path.

### Admission filter

A volume appears in `/media` only when **all** hold:

| Condition | DiskArbitration key |
|-----------|---------------------|
| Is removable or ejectable | `MediaRemovable` or `MediaEjectable` |
| Is not a network volume | `VolumeNetwork` is false |
| Is actually mounted | `VolumePath` is present |
| Is not the boot volume | `VolumePath` is not `/` |

Disk images mount as non-removable local volumes and are excluded, matching
Linux, where a loopback-mounted image does not appear in `/media` either.

### Naming

macOS volume names and Linux path components have incompatible rules, so labels
must be translated rather than copied. The target is what udisks2 would produce
for the same device:

| Case | macOS | udisks2 / mSL/XNU |
|------|-------|-------------------|
| Space in name | `/Volumes/My Disk` | `My\x20Disk` |
| `/` in name | shown as `:` in POSIX paths | percent/hex-escaped |
| Duplicate labels | `UNTITLED`, `UNTITLED 1` | `UNTITLED`, `UNTITLED_1` |
| No label | `Untitled` | filesystem UUID, or `disk` |
| Leading `.` | permitted | escaped (must not be hidden) |

The symlink *target* is the real macOS path, spaces and all; only the name
within `/media/<user>/` is sanitised. This keeps the link resolvable while the
path a Linux program sees follows Linux rules.

### User attribution

Linux attributes a mount to the session that requested it. macOS has no
equivalent notion for `/Volumes` — a mount is system-wide. mSL/XNU attributes
each volume to **the console user at mount time** (the user owning the active
graphical session, as reported by `SCDynamicStoreCopyConsoleUser`), which
reproduces the single-user desktop case exactly and is a defensible
approximation for the rest.

Where no console user exists — mounts during boot, or over SSH with nobody
logged in — the volume is attributed to the console user when one next appears,
rather than being dropped.

## Pseudo-filesystems (detected, not managed)

`/proc` and `/sys` are dynamic: their contents are generated per read, from live
kernel state, and cannot be symlinks. They are separate projects with their own
kernel extensions, installers, and toggles.

mSL/XNU **reports their status and never changes it.** The menu bar shows
whether each is present and mounted; it offers no control over them. Two
applications independently mounting and unmounting the same path would produce
races and status displays that contradict the actual system.

Detection is by mount table inspection, matching the filesystem type at the
expected mount point:

```
proc on /proc (procfs, local, nodev, noexec, nosuid, noatime)
```

Three states are distinguished, because they call for different user guidance:
not installed, installed but not mounted, and mounted.

## Directories requiring no work

These already exist on macOS with correct UNIX semantics and standard contents:
`/bin`, `/sbin`, `/usr`, `/etc`, `/var`, `/tmp`, `/dev`, `/opt`.

Several are themselves symlinks into `/private` (`/etc` → `/private/etc`,
`/tmp` → `/private/tmp`, `/var` → `/private/var`), which is invisible to
everything that matters — programs resolve them transparently.

They are hidden from Finder by a filesystem flag on the sealed system volume,
which SIP does not permit clearing. This affects exactly one application's
presentation and nothing else: every shell, script, and program already sees
these paths normally. There is no supported way to change it, so mSL/XNU does
not attempt to.

## Future entries

Cheap to add once the pattern is proven, each needing a skeleton entry and
therefore a reboot:

| Path | Target | Note |
|------|--------|------|
| `/root` | `/var/root` | The macOS superuser home already is `/var/root` |
| `/run` | `/var/run` | Already exists as a real directory with the expected contents |
| `/srv` | empty | Empty on most Linux systems too |

`/lib` and `/lib64` are deliberately **not** planned. macOS has no ELF loader and
no shared libraries at those paths; presenting the directory without usable
contents would be a layout that lies about what the system can run.
