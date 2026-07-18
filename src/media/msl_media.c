/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_media.c
 *
 * The /media component.
 *
 * Linux desktops present removable media as /media/<user>/<label>, created when
 * that user's session mounts the device and removed on eject. macOS mounts
 * everything in /Volumes: internal APFS volumes, external drives, disk images,
 * and SMB/AFP shares, flat and without user attribution. The boot volume is not
 * even a mount there - /Volumes/Macintosh HD is a symlink to /.
 *
 * Faithful emulation therefore needs two things macOS does not supply directly:
 *
 *   A filter. DiskArbitration knows whether a volume is removable, ejectable,
 *   or a network mount, so admission is decided from the device's own
 *   properties rather than from where it happens to be mounted.
 *
 *   An owner. macOS has no per-session mount ownership, so volumes are
 *   attributed to the console user - the one owning the active graphical
 *   session. That reproduces the single-user desktop case exactly and is an
 *   approximation otherwise; see docs/LAYOUT.md.
 *
 * Enumeration here is synchronous (getmntinfo + a DiskArbitration query per
 * volume), which is what a one-shot `mslctl media sync` needs. The daemon will
 * add DARegisterDiskAppearedCallback and friends on top of the same filter, so
 * the two can never disagree about what belongs in /media.
 */
#include "msl.h"
#include "msl_media.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <SystemConfiguration/SystemConfiguration.h>

/* macOS mounts everything here. */
#define VOLUMES_ROOT    "/Volumes/"

#ifndef MEDIA_ROOT
#define MEDIA_ROOT      MSL_DATA_ROOT "/media"
#endif

/* ---------------------------------------------------------------------------
 * Label sanitisation
 * ------------------------------------------------------------------------- */

/*
 * Translate a macOS volume name into a Linux path component.
 *
 * What udisks2 does, and what we match:
 *
 *   - Spaces are kept. This is deliberate and worth stating, because escaping
 *     them looks tidier: a WD drive labelled "My Passport" really does mount at
 *     /media/<user>/My Passport on a Linux desktop, space and all.
 *   - '/' cannot appear in a path component and becomes '_'. macOS renders a
 *     '/' in a volume name as ':' in the POSIX path, so ':' is folded the same
 *     way - the two are the same character seen from different sides.
 *   - A leading '.' would hide the entry, so it becomes '_'.
 *   - An empty or all-whitespace name becomes "disk".
 *
 * Exact parity with udisks2 beyond these rules is unverified - it would need
 * checking against a real Linux desktop, and the remaining cases (unusual
 * control characters, non-UTF-8 labels) are rare enough that guessing at them
 * would add risk rather than fidelity.
 */
bool
msl_media_sanitise(const char *name, char *out, size_t outsz)
{
	size_t n = 0;
	bool any = false;

	if (out == NULL || outsz < 2)
		return false;

	if (name != NULL) {
		for (const char *p = name; *p != '\0' && n < outsz - 1; p++) {
			unsigned char c = (unsigned char)*p;

			/* Control characters have no place in a path component. */
			if (c < 0x20 || c == 0x7f)
				continue;

			if (c == '/' || c == ':')
				out[n++] = '_';
			else
				out[n++] = (char)c;

			if (c != ' ')
				any = true;
		}
	}

	out[n] = '\0';

	/* Trim trailing whitespace, which macOS permits and Linux tools dislike. */
	while (n > 0 && out[n - 1] == ' ')
		out[--n] = '\0';

	if (!any || n == 0) {
		snprintf(out, outsz, "disk");
		return true;
	}

	/* A leading dot would make the volume invisible under /media. */
	if (out[0] == '.')
		out[0] = '_';

	return true;
}

/* ---------------------------------------------------------------------------
 * Admission filter
 * ------------------------------------------------------------------------- */

static bool
cf_bool(CFDictionaryRef d, CFStringRef key, bool dflt)
{
	CFBooleanRef v = CFDictionaryGetValue(d, key);

	if (v == NULL || CFGetTypeID(v) != CFBooleanGetTypeID())
		return dflt;

	return CFBooleanGetValue(v);
}

/*
 * Should this volume appear in /media? Removable or ejectable, not a network
 * mount, and not the root filesystem.
 *
 * Disk images are excluded by this test because they present as non-removable
 * local volumes - which matches Linux, where a loopback-mounted image does not
 * appear in /media either.
 */
static bool
admit(DADiskRef disk, const char *mountpoint)
{
	CFDictionaryRef desc;
	bool ok;

	if (strcmp(mountpoint, "/") == 0)
		return false;

	desc = DADiskCopyDescription(disk);
	if (desc == NULL)
		return false;

	ok = (cf_bool(desc, kDADiskDescriptionMediaRemovableKey, false) ||
	      cf_bool(desc, kDADiskDescriptionMediaEjectableKey, false)) &&
	     !cf_bool(desc, kDADiskDescriptionVolumeNetworkKey, false);

	CFRelease(desc);
	return ok;
}

/* ---------------------------------------------------------------------------
 * Enumeration
 * ------------------------------------------------------------------------- */

/* Has this sanitised label already been used? udisks2 disambiguates with _N. */
static void
deduplicate(struct msl_volume *vols, int count, char *label, size_t labelsz)
{
	char base[MAXPATHLEN];
	int suffix = 1;

	snprintf(base, sizeof(base), "%s", label);

	for (;;) {
		bool clash = false;

		for (int i = 0; i < count; i++) {
			if (strcmp(vols[i].label, label) == 0) {
				clash = true;
				break;
			}
		}

		if (!clash)
			return;

		snprintf(label, labelsz, "%s_%d", base, suffix++);
	}
}

int
msl_media_scan(struct msl_volume *out, int max)
{
	struct statfs *mounts;
	DASessionRef session;
	int n, count = 0;

	n = getmntinfo(&mounts, MNT_NOWAIT);
	if (n <= 0)
		return n == 0 ? 0 : -1;

	session = DASessionCreate(kCFAllocatorDefault);
	if (session == NULL)
		return -1;

	for (int i = 0; i < n && count < max; i++) {
		const char *mp = mounts[i].f_mntonname;
		CFURLRef url;
		DADiskRef disk;
		const char *name;
		char label[MAXPATHLEN];

		/*
		 * Only consider things mounted under /Volumes. A removable device
		 * mounted elsewhere by hand is the administrator's business, and Linux
		 * would not list it in /media either.
		 */
		if (strncmp(mp, VOLUMES_ROOT, sizeof(VOLUMES_ROOT) - 1) != 0)
			continue;

		url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
		    (const UInt8 *)mp, (CFIndex)strlen(mp), true);
		if (url == NULL)
			continue;

		disk = DADiskCreateFromVolumePath(kCFAllocatorDefault, session, url);
		CFRelease(url);
		if (disk == NULL)
			continue;

		if (!admit(disk, mp)) {
			CFRelease(disk);
			continue;
		}
		CFRelease(disk);

		/* The label comes from the mount point's last component. */
		name = strrchr(mp, '/');
		name = (name != NULL) ? name + 1 : mp;

		if (!msl_media_sanitise(name, label, sizeof(label)))
			continue;

		deduplicate(out, count, label, sizeof(label));

		snprintf(out[count].path, sizeof(out[count].path), "%s", mp);
		snprintf(out[count].label, sizeof(out[count].label), "%s", label);
		count++;
	}

	CFRelease(session);
	return count;
}

/* ---------------------------------------------------------------------------
 * Console user
 * ------------------------------------------------------------------------- */

/*
 * The user owning the active graphical session, which is who a Linux desktop
 * would have attributed the mount to. Returns false when nobody is logged in
 * graphically - at the login window, or over SSH with no console session.
 */
static bool
console_user(char *out, size_t outsz)
{
	CFStringRef name;
	uid_t uid = 0;
	gid_t gid = 0;
	bool ok;

	name = SCDynamicStoreCopyConsoleUser(NULL, &uid, &gid);
	if (name == NULL)
		return false;

	ok = CFStringGetCString(name, out, (CFIndex)outsz, kCFStringEncodingUTF8);
	CFRelease(name);

	if (!ok || out[0] == '\0')
		return false;

	/*
	 * "loginwindow" is reported between logout and the next login. It is not a
	 * user, and creating /media/loginwindow would be nonsense.
	 */
	if (strcmp(out, "loginwindow") == 0)
		return false;

	return true;
}

/* ---------------------------------------------------------------------------
 * The symlink farm
 * ------------------------------------------------------------------------- */

/* Is `path` a symlink of ours - one pointing into /Volumes? */
static bool
ours(const char *path, char *target, size_t target_len)
{
	struct stat sb;
	ssize_t n;

	if (lstat(path, &sb) != 0 || !S_ISLNK(sb.st_mode))
		return false;

	n = readlink(path, target, target_len - 1);
	if (n < 0)
		return false;
	target[n] = '\0';

	return strncmp(target, VOLUMES_ROOT, sizeof(VOLUMES_ROOT) - 1) == 0;
}

/* Remove symlinks under `dir` that are ours but not in `keep`. */
static int
prune(const char *dir, const struct msl_volume *keep, int nkeep)
{
	DIR *d;
	struct dirent *ent;
	char path[MAXPATHLEN], target[MAXPATHLEN];
	int removed = 0;

	d = opendir(dir);
	if (d == NULL)
		return 0;

	while ((ent = readdir(d)) != NULL) {
		bool wanted = false;

		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

		if (!ours(path, target, sizeof(target)))
			continue;

		for (int i = 0; i < nkeep; i++) {
			if (strcmp(keep[i].label, ent->d_name) == 0 &&
			    strcmp(keep[i].path, target) == 0) {
				wanted = true;
				break;
			}
		}

		if (wanted)
			continue;

		if (unlink(path) != 0) {
			msl_err("cannot remove %s: %s", path, strerror(errno));
			continue;
		}

		msl_log("  unlinked %s", path);
		removed++;
	}

	closedir(d);
	return removed;
}

int
msl_media_sync(void)
{
	struct msl_volume vols[64];
	char user[256], dir[MAXPATHLEN], path[MAXPATHLEN], target[MAXPATHLEN];
	int n;

	if (!msl_is_root()) {
		msl_err("syncing /media requires root");
		return -1;
	}

	if (msl_state_get(MSL_MEDIA_STATE, 0) == 0)
		return 0;	/* component is off */

	if (!console_user(user, sizeof(user))) {
		/*
		 * No graphical session, so there is nobody to attribute a mount to.
		 * Existing links are left in place rather than torn down: the user is
		 * most likely between sessions, and their media has not gone anywhere.
		 */
		msl_log("no console user; leaving /media unchanged");
		return 0;
	}

	if (mkdir(MEDIA_ROOT, 0755) != 0 && errno != EEXIST) {
		msl_err("cannot create %s: %s", MEDIA_ROOT, strerror(errno));
		return -1;
	}

	snprintf(dir, sizeof(dir), "%s/%s", MEDIA_ROOT, user);
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
		msl_err("cannot create %s: %s", dir, strerror(errno));
		return -1;
	}

	n = msl_media_scan(vols, (int)(sizeof(vols) / sizeof(vols[0])));
	if (n < 0) {
		msl_err("cannot enumerate volumes");
		return -1;
	}

	for (int i = 0; i < n; i++) {
		struct stat sb;

		snprintf(path, sizeof(path), "%s/%s", dir, vols[i].label);

		if (lstat(path, &sb) == 0) {
			if (ours(path, target, sizeof(target)) &&
			    strcmp(target, vols[i].path) == 0)
				continue;	/* already correct */

			if (!ours(path, target, sizeof(target))) {
				msl_err("skipping %s: not a symlink of ours", path);
				continue;
			}

			if (unlink(path) != 0) {
				msl_err("cannot replace %s: %s", path, strerror(errno));
				continue;
			}
		}

		if (symlink(vols[i].path, path) != 0) {
			msl_err("cannot create %s -> %s: %s", path, vols[i].path,
			    strerror(errno));
			continue;
		}

		msl_log("  linked %s -> %s", path, vols[i].path);
	}

	prune(dir, vols, n);
	return 0;
}

/* ---------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

int
msl_media_status(struct msl_media_status *st)
{
	struct msl_volume vols[64];
	char dir[MAXPATHLEN], path[MAXPATHLEN], target[MAXPATHLEN];
	DIR *d;
	struct dirent *ent;
	int n;

	memset(st, 0, sizeof(*st));

	st->enabled = msl_state_get(MSL_MEDIA_STATE, 0) != 0;

	if (msl_skeleton_status(MSL_MEDIA_NAME, &st->skel) != 0)
		return -1;

	st->reboot_pending = msl_skeleton_reboot_pending(MSL_MEDIA_NAME);

	if (console_user(st->user, sizeof(st->user)))
		snprintf(dir, sizeof(dir), "%s/%s", MEDIA_ROOT, st->user);
	else
		dir[0] = '\0';

	n = msl_media_scan(vols, (int)(sizeof(vols) / sizeof(vols[0])));
	st->volumes = (n < 0) ? 0 : n;

	if (dir[0] != '\0' && (d = opendir(dir)) != NULL) {
		while ((ent = readdir(d)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;
			snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
			if (!ours(path, target, sizeof(target)))
				continue;

			st->links++;

			/*
			 * A link whose target has gone: the volume was ejected without a
			 * sync. Until the daemon watches DiskArbitration this is the
			 * normal outcome of ejecting anything, so it is worth reporting
			 * rather than leaving as a silent inconsistency between the
			 * volume and link counts.
			 */
			if (access(target, F_OK) != 0)
				st->stale++;
		}
		closedir(d);
	}

	return 0;
}

int
msl_media_enable(void)
{
	int rc;

	if (!msl_is_root()) {
		msl_err("enabling /media requires root");
		return -1;
	}

	rc = msl_skeleton_add(MSL_MEDIA_NAME);
	if (rc < 0)
		return -1;

	if (msl_state_set(MSL_MEDIA_STATE, 1) != 0)
		msl_err("warning: could not persist state: %s", strerror(errno));

	if (rc == 1)
		msl_log("/media will appear after the next reboot.");

	/*
	 * Populate now regardless. The symlinks live on the Data volume and can be
	 * built before the root entry exists, so everything is already in place
	 * when the reboot makes /media reachable.
	 */
	return msl_media_sync();
}

int
msl_media_disable(void)
{
	char user[256], dir[MAXPATHLEN];
	int rc;

	if (!msl_is_root()) {
		msl_err("disabling /media requires root");
		return -1;
	}

	if (console_user(user, sizeof(user))) {
		snprintf(dir, sizeof(dir), "%s/%s", MEDIA_ROOT, user);
		msl_log("removing symlinks from %s", dir);
		prune(dir, NULL, 0);
	}

	rc = msl_skeleton_remove(MSL_MEDIA_NAME);
	if (rc < 0)
		return -1;

	if (msl_state_set(MSL_MEDIA_STATE, 0) != 0)
		msl_err("warning: could not persist state: %s", strerror(errno));

	if (rc == 1)
		msl_log("/media will disappear after the next reboot.");

	return 0;
}
