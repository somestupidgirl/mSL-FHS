/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_visibility.c
 *
 * Finder visibility of root-level directories. See fhs_visibility.h for the
 * measured behaviour this is built around.
 */
#include "fhs.h"
#include "fhs_visibility.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * UF_HIDDEN is the only flag involved. There is deliberately no system-level
 * counterpart - <sys/stat.h> says so outright: "NOTE: There is no SF_HIDDEN
 * bit." So a hidden entry is always hidden by a user flag, and the question is
 * only ever whether the filesystem will let us clear it.
 */
#define HIDDEN_FLAGS    UF_HIDDEN

/*
 * Alphabetical, as the Finder and the menu present them - not grouped by
 * origin, because a user looking for a directory knows its name and not
 * whether macOS or this layer put it there.
 */
const struct fhs_node fhs_root_nodes[] = {
	{ "/Applications", false },
	{ "/bin",          false },
	{ "/boot",         true  },
	{ "/cores",        false },
	{ "/dev",          false },
	{ "/etc",          false },
	{ "/home",         true  },
	{ "/Library",      false },
	{ "/media",        true  },
	{ "/mnt",          true  },
	{ "/opt",          false },
	{ "/private",      false },
	{ "/root",         true  },
	{ "/run",          true  },
	{ "/sbin",         false },
	{ "/srv",          true  },
	{ "/tmp",          false },
	{ "/usr",          false },
	{ "/var",          false },
	{ "/Volumes",      false },
};

const size_t fhs_root_node_count =
    sizeof(fhs_root_nodes) / sizeof(fhs_root_nodes[0]);

const struct fhs_node *
fhs_node_find(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;

	for (size_t i = 0; i < fhs_root_node_count; i++) {
		const char *path = fhs_root_nodes[i].path;

		if (strcmp(path, name) == 0)
			return &fhs_root_nodes[i];

		/* Accept a bare name, so "opt" finds "/opt". */
		if (name[0] != '/' && strcmp(path + 1, name) == 0)
			return &fhs_root_nodes[i];
	}

	return NULL;
}

const char *
fhs_vis_lock_reason(enum fhs_vis_lock lock)
{
	switch (lock) {
	case FHS_VIS_CHANGEABLE:
		return NULL;
	case FHS_VIS_ABSENT:
		return "does not exist";
	case FHS_VIS_SIP:
		return "protected by System Integrity Protection";
	case FHS_VIS_READONLY:
		return "the entry is on the sealed system volume";
	case FHS_VIS_UNSUPPORTED:
		return "this filesystem ignores visibility flags";
	case FHS_VIS_PROTECTED:
		return "the system refuses to change this entry";
	}
	return "unknown";
}

/*
 * Which filesystem holds the directory *entry*, as opposed to whatever it
 * points at. For a symlink at /, the entry is on the root filesystem even when
 * its target is elsewhere - which is precisely why /home cannot be unhidden
 * while /opt can, despite both appearing to be "a thing at the root".
 */
static int
entry_fs(const char *path, bool symlink, struct statfs *out)
{
	char parent[PATH_MAX];
	char *slash;

	if (!symlink)
		return statfs(path, out);

	/*
	 * statfs() follows symlinks, so asking about the link itself means asking
	 * about the directory holding it. For /home that is /, the sealed root -
	 * which is why /home cannot be unhidden while /opt can.
	 */
	snprintf(parent, sizeof(parent), "%s", path);
	slash = strrchr(parent, '/');

	if (slash == parent)
		parent[1] = '\0';       /* "/home" -> "/" */
	else if (slash != NULL)
		*slash = '\0';          /* "/a/b"  -> "/a" */
	else
		snprintf(parent, sizeof(parent), ".");

	return statfs(parent, out);
}

/*
 * Paths that pass every test above and are still refused.
 *
 * /private is the known case: it carries no SF_RESTRICTED flag and no
 * com.apple.rootless attribute, and it firmlinks to the writable Data volume,
 * so nothing observable says it is protected - yet chflags returns EPERM.
 * Whatever enforces that is not introspectable from userspace, so the entry is
 * recorded from measurement rather than derived.
 *
 * This list is a display hint: it stops the GUI offering a toggle that cannot
 * work. It is not the safety mechanism - fhs_vis_set() verifies every change
 * regardless, so an unlisted path that turns out to be protected is reported
 * accurately even though it was offered.
 */
static const char *const protected_paths[] = {
	"/private",
	NULL,
};

static bool
is_protected(const char *path)
{
	for (int i = 0; protected_paths[i] != NULL; i++) {
		if (strcmp(protected_paths[i], path) == 0)
			return true;
	}
	return false;
}

int
fhs_vis_status(const char *path, struct fhs_vis_status *st)
{
	struct stat sb;
	struct statfs fs;

	if (path == NULL || st == NULL) {
		errno = EINVAL;
		return -1;
	}

	memset(st, 0, sizeof(*st));

	if (lstat(path, &sb) != 0) {
		st->lock = FHS_VIS_ABSENT;
		return 0;
	}

	st->exists = true;
	st->symlink = S_ISLNK(sb.st_mode);
	st->hidden = (sb.st_flags & HIDDEN_FLAGS) != 0;

	if (entry_fs(path, st->symlink, &fs) == 0) {
		snprintf(st->fstype, sizeof(st->fstype), "%s", fs.f_fstypename);

		/* A mount point of its own, rather than a directory on its parent. */
		st->is_mount = !st->symlink && strcmp(fs.f_mntonname, path) == 0;
		st->browsable = (fs.f_flags & MNT_DONTBROWSE) == 0;
	} else {
		st->browsable = true;
	}

	/*
	 * Classify in order of how absolutely each blocks the change. SIP first:
	 * it refuses regardless of anything else about the volume.
	 */
	if (sb.st_flags & SF_RESTRICTED)
		st->lock = FHS_VIS_SIP;
	else if (is_protected(path))
		st->lock = FHS_VIS_PROTECTED;
	else if (strcmp(st->fstype, "devfs") == 0)
		st->lock = FHS_VIS_UNSUPPORTED;
	else if (st->fstype[0] != '\0' && (fs.f_flags & MNT_RDONLY) != 0)
		st->lock = FHS_VIS_READONLY;
	else
		st->lock = FHS_VIS_CHANGEABLE;

	return 0;
}

int
fhs_vis_set(const char *path, bool hidden, char *reason, size_t reason_len)
{
	struct fhs_vis_status before, after;
	u_int flags;
	struct stat sb;

	if (fhs_vis_status(path, &before) != 0) {
		if (reason != NULL)
			snprintf(reason, reason_len, "invalid path");
		return -1;
	}

	if (!before.exists) {
		if (reason != NULL)
			snprintf(reason, reason_len, "%s does not exist", path);
		return -1;
	}

	if (before.hidden == hidden)
		return 0;	/* already as requested */

	/*
	 * Report a known-blocked node without attempting the call. Trying anyway
	 * would produce the same outcome with a worse message - "Operation not
	 * permitted" says nothing about SIP being the reason.
	 */
	if (before.lock != FHS_VIS_CHANGEABLE) {
		if (reason != NULL)
			snprintf(reason, reason_len, "%s: %s", path,
			    fhs_vis_lock_reason(before.lock));
		return -1;
	}

	if (lstat(path, &sb) != 0) {
		if (reason != NULL)
			snprintf(reason, reason_len, "%s: %s", path, strerror(errno));
		return -1;
	}

	/*
	 * UF_HIDDEN is a *user* flag, so its owner may set it without privilege -
	 * root is needed only because the root-level entries belong to root. The
	 * check is therefore on ownership rather than on being root, which both
	 * states the real requirement and gives a better message than the EPERM
	 * the syscall would otherwise produce.
	 */
	if (geteuid() != 0 && geteuid() != sb.st_uid) {
		if (reason != NULL)
			snprintf(reason, reason_len,
			    "%s is owned by uid %u; changing it requires root",
			    path, sb.st_uid);
		return -1;
	}

	/*
	 * Only UF_HIDDEN is ours to touch. SF_HIDDEN is a system flag; preserving
	 * whatever else is set (sunlnk, nodump, ...) matters because these entries
	 * are macOS's, not ours.
	 */
	flags = hidden ? (sb.st_flags | UF_HIDDEN) : (sb.st_flags & ~UF_HIDDEN);

	/* lchflags, not chflags: for a symlink we mean the link, not its target. */
	if (lchflags(path, flags) != 0) {
		if (reason != NULL)
			snprintf(reason, reason_len, "%s: %s", path, strerror(errno));
		return -1;
	}

	/*
	 * Verify. devfs accepts the call, returns success, and changes nothing -
	 * so a caller that trusted the return value would report /dev as revealed
	 * while the Finder went on hiding it. Anything that claims to have worked
	 * has to prove it.
	 */
	if (fhs_vis_status(path, &after) == 0 && after.hidden != hidden) {
		if (reason != NULL)
			snprintf(reason, reason_len,
			    "%s: the filesystem accepted the change and ignored it", path);
		return -1;
	}

	return 0;
}

int
fhs_vis_set_browsable(const char *path, bool browsable,
                      char *reason, size_t reason_len)
{
	struct fhs_vis_status before, after;
	const char *const argv[] = {
		"/sbin/mount", "-u", "-o", browsable ? "browse" : "nobrowse", path, NULL
	};
	int rc;

	if (fhs_vis_status(path, &before) != 0 || !before.exists) {
		if (reason != NULL)
			snprintf(reason, reason_len, "%s does not exist", path);
		return -1;
	}

	if (!before.is_mount) {
		if (reason != NULL)
			snprintf(reason, reason_len,
			    "%s is not a mount point, so it has no browse flag", path);
		return -1;
	}

	/*
	 * devfs does not implement MNT_UPDATE - `mount -u` on it fails outright
	 * with "option not supported", so its flags cannot be changed after it is
	 * mounted. Refusing here gives the real reason instead of surfacing
	 * mount(8)'s exit status.
	 *
	 * Together with devfs ignoring chflags, this closes both routes to
	 * revealing /dev: the hidden flag cannot be cleared and the nobrowse flag
	 * cannot be changed. See fhs_visibility.h.
	 */
	if (strcmp(before.fstype, "devfs") == 0) {
		if (reason != NULL)
			snprintf(reason, reason_len,
			    "%s: devfs does not support changing mount options after "
			    "mounting, so its nobrowse flag cannot be cleared", path);
		return -1;
	}

	if (before.browsable == browsable)
		return 0;	/* already as requested */

	if (!fhs_is_root()) {
		if (reason != NULL)
			snprintf(reason, reason_len, "changing the browse flag requires root");
		return -1;
	}

	/*
	 * mount -u updates the existing mount in place rather than unmounting and
	 * remounting it. That distinction matters a great deal for /dev, where an
	 * unmount would take the system's device nodes with it.
	 */
	rc = fhs_run(argv);
	if (rc != 0) {
		if (reason != NULL)
			snprintf(reason, reason_len,
			    "%s: mount -u exited %d", path, rc);
		return -1;
	}

	/* Verify, for the same reason fhs_vis_set() does. */
	if (fhs_vis_status(path, &after) == 0 && after.browsable != browsable) {
		if (reason != NULL)
			snprintf(reason, reason_len,
			    "%s: the mount accepted the change and ignored it", path);
		return -1;
	}

	return 0;
}
