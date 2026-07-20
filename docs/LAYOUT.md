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
| `/home/<user>` | Per-user home directories | `/Users/<user>` | Skeleton symlink + farm; `auto_home` masked | Yes | **Working** |
| `/mnt` | Scratch for manual mounts | — | Skeleton symlink; mounts reported | Yes | **Working** |
| `/media/<user>/<label>` | Removable media, per session | `/Volumes/<label>`, filtered | Symlink farm from DiskArbitration | Yes | **Working** |
| `/proc` | Process pseudo-filesystem | — | External kext; **detected only** | n/a | **Working** (detection) |
| `/sys` | Kernel object pseudo-filesystem | — | External; **detected only** | n/a | **Working** (detection) |
| `/root` | Superuser home | `/var/root` | Skeleton symlink to existing content | Yes | **Working** |
| `/run` | Runtime state | `/var/run` | Skeleton symlink to existing content | Yes | **Working** |
| `/srv` | Data served by this system | — | Skeleton symlink; stays empty | Yes | **Working** |
| `/opt` | Optional packages | `/opt` | **Already exists**; visibility changeable | — | Nothing to do |
| `/bin` `/sbin` `/usr` `/etc` `/dev` `/tmp` `/var` | Standard UNIX | Themselves | **Already correct**; visibility locked | — | Nothing to do |

Finder visibility is a separate axis from the layout, and applies to every node
above. See [Finder visibility](#finder-visibility).

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

Because the first form is read-only, mSL/XNU uses the second exclusively:

```
home	/System/Volumes/Data/home
mnt	/System/Volumes/Data/mnt
media	/System/Volumes/Data/media
srv	/System/Volumes/Data/srv
root	/var/root
run	/var/run
```

Most entries point at a directory of the same name on the writable Data volume —
an ordinary writable directory the layer owns, whose contents the daemon
maintains with nothing more than `symlink(2)` and `unlink(2)`. This mirrors what
macOS itself does for `/home`, which is a symlink to
`/System/Volumes/Data/home`, created at boot by autofs rather than shipped in
the system image.

The last two are different in kind: they point at directories macOS already has,
so the entry is a new name for existing content rather than a new place to put
things. See [`/root`, `/run` and `/srv`](#root-run-and-srv).

Constraints worth knowing:

- An entry **cannot shadow a path that already exists** at `/`. This is why
  `/opt` needs no skeleton entry — it is genuinely part of the system image.
  `/home` is the instructive case: it *looks* like it already exists, but is
  created each boot by autofs from a map this layer masks, so it does need an
  entry of its own. See [`/home`](#home).
- The target of a symlink entry is not created for you. For an entry the layer
  owns, the component creates the Data-volume directory before the reboot that
  activates it. For an entry naming existing content, the target must already
  exist and is **never** created — the declaration is refused instead, so a
  symlink at `/` cannot be left dangling.
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

**Mechanism.** Declare our own `/home`, mask the map, then maintain a symlink
farm in the directory it was occupying:

1. Add `home` → `/System/Volumes/Data/home` to `/etc/synthetic.conf`
2. Comment out the `/home` line in `/etc/auto_master` (backing up the original)
3. `automount -vc` to flush the automounter, releasing the directory
4. Create `/System/Volumes/Data/home/<user>` → `/Users/<user>` per local user

**Step 1 is not optional, and an earlier design omitted it.** The root-level
`/home` is *not* a permanent part of macOS: autofs creates it at boot from the
same `auto_master` line that step 2 masks. Masking the map therefore removes
`/home` itself at the next boot, leaving the farm below it with nothing pointing
at it — the farm survives intact, and every path through it breaks.

That failure is invisible for exactly one boot, which is what made it
misleading: the entry autofs had already created stays for the rest of the
session, so enabling appears to work and only the next start reveals otherwise.
An `EROFS` from `chflags /home` was read at the time as proof the symlink was
baked into the sealed system volume and therefore permanent; it means only that
the root directory is read-only.

So `/home` needs a reboot like `/mnt` and `/media`, and is **not** an exception
to the skeleton rule.

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
  writable, so enabling completes in one pass. The implementation still detects
  and reports the case where the mount is not released, rather than emitting a
  symlink error per account.
- **Paths resolve, and writes land in the real home.** `/home/<user>/…` opens
  the same files as `/Users/<user>/…`, as it must, being one directory.
- **`/home` survives a reboot.** Confirmed by the entry being recreated at boot
  with a timestamp later than the enable that declared it, with no autofs mount
  present — so `apfs_boot_util` made it from `synthetic.conf`, and the ordering
  holds: `synthetic.conf` is processed before autofs, and the masked map does
  not contend for the name.

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

### Reporting what is mounted

Empty by default does not mean empty forever: the point of `/mnt` is that an
administrator mounts things into it. The component reports whatever is there, so
the interface can show it instead of claiming the directory is empty:

```
disk1 mounted at /mnt/disk1 hfs
```

with "No mount points detected" when there is nothing. This is **read-only** —
mSL never mounts or unmounts anything under `/mnt`, and never creates the mount
point directories. That remains the administrator's job, exactly as on Linux.

Two details decide whether the detection is correct:

- **Both spellings must be matched.** `/mnt` is a symlink to the Data volume,
  and the kernel records a mount under whichever path it was requested with.
  Mounting at `/mnt/disk1` is recorded as `/System/Volumes/Data/mnt/disk1` —
  confirmed on a real mount — so matching only the literal `/mnt/` prefix would
  report nothing at all. Results are normalised back to `/mnt/<name>`.
- **Only direct children count.** A nested `/mnt/a/b` is not a `/mnt` mount
  point in the sense an administrator means, and lookalike prefixes (`/mntfoo`,
  `/mnt2/disk`) must not match a naive string comparison.

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

**Disk images are admitted.** This was initially specified the other way round,
on the assumption that macOS reports them as non-removable. It does not —
verified by attaching one:

```
DADeviceModel    = "Disk Image"
DADeviceProtocol = "Virtual Interface"
DAMediaRemovable = 1
DAMediaEjectable = 1
```

Admitting them is also the more faithful outcome. Mounting an ISO from a Linux
file manager creates a udisks2 loop device and *does* appear at
`/media/<user>/<label>`, so excluding them here would have diverged from Linux
rather than matched it.

Should they ever need excluding, `DADeviceProtocol` is `"Virtual Interface"` for
image-backed volumes and names the real bus (`USB`, `Thunderbolt`, …) otherwise,
which distinguishes them cleanly.

### Naming

macOS volume names and Linux path components have incompatible rules, so labels
must be translated rather than copied. The target is what udisks2 would produce
for the same device:

| Case | macOS | udisks2 / mSL/XNU |
|------|-------|-------------------|
| Space in name | `/Volumes/My Disk` | `My Disk` — **kept** |
| `/` in name | shown as `:` in POSIX paths | `_` |
| Duplicate labels | `UNTITLED`, `UNTITLED 1` | `UNTITLED`, `UNTITLED_1` |
| No label | `Untitled` | `disk` |
| Leading `.` | permitted | `_` (must not be hidden) |
| Control characters | permitted | stripped |

Spaces being *kept* is worth stating, because escaping them looks tidier and an
earlier draft of this document specified `\x20`. That was wrong: a WD drive
labelled `My Passport` genuinely mounts at `/media/<user>/My Passport` on a
Linux desktop, space and all.

Beyond these rules, exact udisks2 parity is **unverified** — it would need
checking against a real Linux desktop. The remaining cases (non-UTF-8 labels,
exotic Unicode) are rare enough that guessing would add risk rather than
fidelity, so the rules above are deliberately conservative.

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

## `/root`, `/run` and `/srv`

Three nodes that are nothing but a skeleton entry. `/home`, `/mnt` and `/media`
each have real work behind them — an automounter to mask, mounts to report,
volumes to track — and are their own components; these differ only in a name and
a target, so they share one table-driven implementation.

| Path | Target | What it is |
|------|--------|------------|
| `/root` | `/var/root` | The superuser's home directory |
| `/run` | `/var/run` | Runtime state — pid files, sockets |
| `/srv` | `/System/Volumes/Data/srv` | Data served by this system; empty |

### Naming existing content

`/root` and `/run` are the interesting ones, and they work differently from
every other component here: **they create no new storage.** macOS already has
the superuser's home at `/var/root` and runtime state at `/var/run` — the same
things Linux calls `/root` and `/run`. The entry gives existing content the name
Linux uses, and nothing else. `ls /run` lists the live pid files and sockets the
system is actually using.

This is the layer at its cheapest and most honest: no synchronisation, no
daemon involvement, no second copy that could drift.

`/srv` has no macOS equivalent. It is an ordinary empty directory on the Data
volume, which is what `/srv` is on most Linux systems too — the FHS reserves it
for site-specific served data, and nothing populates it automatically.

### Never taking ownership of a system directory

Pointing at a macOS directory required a rule the skeleton did not previously
need. Every earlier entry pointed at `MSL_DATA_ROOT/<name>`, which the layer
creates and owns, so the skeleton simply created its target. `mkdir`-ing over
`/var/root` would be a different act entirely: taking possession of a directory
that is macOS's, with its own ownership and modes (`/var/root` is `0750`,
root-only — as `/root` is on Linux).

So the skeleton now takes an explicit `create_target` flag, and:

- a node that owns its target creates it, as before;
- a node that points at existing content **never** creates it, and its
  declaration is **refused** if the target is missing rather than left to
  dangle.

The test suite asserts this structurally rather than by inspection: any node
marked as creating its target must have that target under the layer's own
Data-volume path.

For `/srv`, a missing target while the component is disabled is simply the
state before it is switched on, so only a node depending on an existing
directory — or an enabled `/srv` — reports a missing target as a fault.

### Verified behaviour

Confirmed across a reboot on macOS 26.5.2:

- All three appear at `/`, and all report `active`, with no reboot pending.
- `/root` resolves to the real `/var/root`, mode `0750`.
- `/run` lists live runtime state.
- `/srv` is present and empty.
- **A target behind another symlink resolves.** `/run` → `/var/run`, and `/var`
  is itself a symlink to `/private/var`. `synthetic.conf` stores the target as
  text and the kernel resolves it at use, so the double indirection is invisible.

## Finder visibility

A separate axis from the layout: not *which* directories exist, but which of
them the Finder shows. It applies to every root-level node, whether macOS
provides it or this layer does.

macOS hides most root entries with the **`UF_HIDDEN`** file flag. Cmd-Shift-.
reveals flagged items but draws them dimmed; clearing the flag makes an entry
ordinary. `/.hidden`, the mechanism older releases used, does not exist on
current macOS — the flag is the whole story. There is deliberately no
system-level counterpart: `<sys/stat.h>` states outright that there is no
`SF_HIDDEN` bit.

### What can actually be changed

Whether the flag can be cleared depends on where the *directory entry* lives,
which the path does not reveal. Measured on macOS 26.5.2, not inferred:

| Node | Result | Why |
|------|--------|-----|
| `/opt` `/cores` `/Volumes` | **Changeable** | firmlinked to the writable Data volume |
| `/home` and other root symlinks | `EROFS` | the entry's parent is the sealed root |
| `/private` | `EPERM` | no observable cause; see below |
| `/bin` `/etc` `/sbin` `/tmp` `/usr` `/var` | `EPERM` | `SF_RESTRICTED` — SIP |
| `/dev` | silently ignored | devfs accepts the call and does nothing |

Clearing `/opt`'s flag makes it **fully** visible in the Finder, not merely
dimmed — confirmed visually.

The classification exists so the interface can disable a toggle that cannot
work and say why, rather than offering a control that does nothing. It is a
display hint, not the safety mechanism: **every change is verified by re-reading
the flag afterwards**, so a filesystem that accepts a change and ignores it is
reported as a failure rather than as success.

`/private` is recorded from measurement rather than derived. It carries no
`SF_RESTRICTED` flag and no `com.apple.rootless` attribute, and firmlinks to a
writable volume — nothing observable marks it as protected, yet the change is
refused. Whatever enforces that is not introspectable from userspace.

### `/dev` cannot be shown

`/dev` is hidden twice over, and both mechanisms are closed. All three routes
were tried on a real system:

| Route | Result |
|-------|--------|
| `chflags` to clear `UF_HIDDEN` | accepted and **silently ignored** by devfs |
| `mount -u` to clear `nobrowse` | `MNT_UPDATE` not supported by devfs |
| stack a second devfs over `/dev` | `EPERM` — SIP rootless mount protection, even as root |

The first is the reason every mutation verifies afterwards: `chflags` on `/dev`
returns success and changes nothing, so code trusting the return value would
report `/dev` as revealed while the Finder went on hiding it.

There is therefore no supported way to show `/dev` in the Finder. A kernel
extension is the only remaining path, for one cosmetic directory, so it is
deferred alongside the `sysfs` work rather than attempted here.

### Ownership, not root

`UF_HIDDEN` is a *user* flag: its owner may set and clear it unprivileged. Root
is needed only because the root-level entries belong to root. The check is
therefore on ownership rather than on being root, which states the real
requirement and gives a better message than the bare `EPERM` the syscall would
produce — and it is what allows the whole set-and-verify cycle to be tested
without privilege, on scratch files the test process owns.

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

Most are hidden from the Finder, and whether that can be changed varies per
node — `/opt` can be shown, `/bin` and the rest cannot. See
[Finder visibility](#finder-visibility) for the measurements and the reasons.

Either way this affects exactly one application's presentation and nothing
else: every shell, script, and program already sees these paths normally.
