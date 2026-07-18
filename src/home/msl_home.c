/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_home.c
 *
 * The /home component.
 *
 * macOS keeps home directories in /Users. It does ship a /home, but it is a
 * symlink to /System/Volumes/Data/home, which is an autofs mount point owned by
 * the `auto_home` map declared in /etc/auto_master:
 *
 *     /home			auto_home	-nobrowse,hidefromfinder
 *
 * That map exists to serve network home directories; on a machine with only
 * local accounts it resolves nothing (its od_user_homes helper returns no
 * records for a local user), so /home is an empty, read-only directory. While
 * the map is mounted it also owns the directory, so nothing can be created
 * there.
 *
 * This component masks that line, flushes the automounter to release the mount,
 * and maintains a farm of symlinks in the now-writable directory:
 *
 *     /home/sunneva -> /Users/sunneva
 *
 * Because the /home symlink already exists and its target is on the writable
 * Data volume, no /etc/synthetic.conf entry is needed and no reboot is
 * required - enable and disable both take effect immediately.
 *
 * Two safety rules govern everything below:
 *
 *   1. The auto_master line is *masked*, not deleted, and marked with a
 *      distinctive prefix, so unmasking restores it exactly and unrelated edits
 *      to the file survive a disable/enable cycle. A pristine copy is kept at
 *      /var/db/msl.auto_master.orig regardless.
 *
 *   2. Nothing is ever removed from /home unless it is a symlink pointing into
 *      /Users. A real directory found there is left untouched and reported, on
 *      the assumption that it is someone's data and not ours to delete.
 */
#include "msl.h"
#include "msl_home.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * System paths. Overridable at compile time so the rewrite logic can be
 * exercised against copies in a scratch directory, rather than requiring a
 * test to edit the live automounter configuration.
 */
#ifndef AUTO_MASTER
#define AUTO_MASTER     "/etc/auto_master"
#endif
#ifndef AUTO_HOME
#define AUTO_HOME       "/etc/auto_home"
#endif
#ifndef AUTO_BACKUP
#define AUTO_BACKUP     MSL_STATE_DIR "/msl.auto_master.orig"
#endif
#define AUTOMOUNT       "/usr/sbin/automount"

/*
 * Prefix stamped onto the auto_master line we mask. Distinctive enough that we
 * never mistake a user's own commented-out line for ours, and vice versa.
 */
#define MASK_MARK       "#msl:disabled# "

/* Where real home directories live on macOS. */
#define USERS_ROOT      "/Users/"

/* Not an account; macOS's shared folder happens to live among the users. */
#define USERS_SHARED    "/Users/Shared"

/* ---------------------------------------------------------------------------
 * Account enumeration
 * ------------------------------------------------------------------------- */

/*
 * Is this a local account a person actually logs into?
 *
 * Three kinds of account have to be excluded, and each needs its own test:
 *
 *   - macOS service accounts (_locationd, _spotlight, ...): uid below
 *     MSL_MIN_UID, and by convention an underscore-prefixed name.
 *   - The `nobody` and `nogroup` sentinels: uid -2 and -1, which are *huge*
 *     when read as the unsigned uid_t they are stored in, so a lower bound
 *     alone lets them through. Hence the upper bound.
 *   - Accounts with a non-login shell.
 *
 * The shell test is a blacklist rather than a whitelist on purpose: a real user
 * may well have an unusual shell (this machine's own account uses Homebrew's
 * bash), and a login account should never be dropped for that.
 */
static bool
is_login_account(const struct passwd *pw)
{
	static const char *const nologin[] = {
		"/usr/bin/false", "/sbin/nologin", "/usr/sbin/nologin", "/dev/null", NULL
	};

	if (pw->pw_uid < MSL_MIN_UID || pw->pw_uid >= 0x7FFFFFFF)
		return false;

	if (pw->pw_name == NULL || pw->pw_name[0] == '_' || pw->pw_name[0] == '\0')
		return false;

	if (pw->pw_shell != NULL) {
		for (int i = 0; nologin[i] != NULL; i++) {
			if (strcmp(pw->pw_shell, nologin[i]) == 0)
				return false;
		}
	}

	return true;
}

/*
 * Is this an account that should get a /home entry? A login account whose home
 * is a direct child of /Users.
 */
static bool
eligible(const struct passwd *pw)
{
	const char *rest;

	if (!is_login_account(pw))
		return false;

	if (pw->pw_dir == NULL || strncmp(pw->pw_dir, USERS_ROOT, sizeof(USERS_ROOT) - 1) != 0)
		return false;

	if (strcmp(pw->pw_dir, USERS_SHARED) == 0)
		return false;

	/* A direct child of /Users only - no nested paths. */
	rest = pw->pw_dir + sizeof(USERS_ROOT) - 1;
	if (*rest == '\0' || strchr(rest, '/') != NULL)
		return false;

	return true;
}

/* ---------------------------------------------------------------------------
 * auto_master masking
 * ------------------------------------------------------------------------- */

/* Does this line declare the /home automounter map? */
static bool
is_home_line(const char *line)
{
	/* First whitespace-delimited field must be exactly "/home". */
	if (strncmp(line, "/home", 5) != 0)
		return false;

	return line[5] == ' ' || line[5] == '\t';
}

/*
 * Bounded append. Returns false if the text would not fit, so the caller can
 * abandon the rewrite rather than write a truncated /etc/auto_master - a
 * silently shortened automounter configuration would be far worse than a
 * failed enable.
 */
static bool
append(char *buf, size_t bufsz, size_t *len, const char *text, size_t text_len)
{
	if (*len + text_len + 1 > bufsz)
		return false;

	memcpy(buf + *len, text, text_len);
	*len += text_len;
	buf[*len] = '\0';
	return true;
}

/*
 * Rewrite /etc/auto_master, masking or unmasking the /home line. Returns 1 if
 * the file was changed, 0 if it was already in the requested state, -1 on
 * error.
 */
static int
set_masked(bool mask)
{
	char *text, *out, *line, *next;
	size_t len, outsz, outlen = 0;
	bool changed = false;
	int rc = -1;

	text = msl_slurp(AUTO_MASTER, &len);
	if (text == NULL) {
		msl_err("cannot read %s: %s", AUTO_MASTER, strerror(errno));
		return -1;
	}

	/* Worst case, every line gains the mask prefix. */
	outsz = len + (len / 8 + 1) * sizeof(MASK_MARK) + 1;
	out = malloc(outsz);
	if (out == NULL) {
		free(text);
		return -1;
	}

	for (line = text; line != NULL && *line != '\0'; line = next) {
		const char *emit = line;
		char saved = '\0';
		char *eol = strchr(line, '\n');

		if (eol != NULL) {
			saved = *eol;
			*eol = '\0';
			next = eol + 1;
		} else {
			next = NULL;
		}

		if (mask && is_home_line(line)) {
			if (!append(out, outsz, &outlen, MASK_MARK, sizeof(MASK_MARK) - 1))
				goto overflow;
			changed = true;
		} else if (!mask && strncmp(line, MASK_MARK, sizeof(MASK_MARK) - 1) == 0) {
			emit = line + sizeof(MASK_MARK) - 1;
			changed = true;
		}

		if (!append(out, outsz, &outlen, emit, strlen(emit)))
			goto overflow;

		if (eol != NULL) {
			*eol = saved;
			if (!append(out, outsz, &outlen, "\n", 1))
				goto overflow;
		}
	}

	if (!changed) {
		rc = 0;
		goto done;
	}

	/* Keep a pristine copy before the first modification, for recovery. */
	if (mask && msl_backup_once(AUTO_MASTER, AUTO_BACKUP) != 0)
		msl_err("warning: could not back up %s: %s", AUTO_MASTER, strerror(errno));

	if (msl_write_atomic(AUTO_MASTER, out, outlen, 0644) != 0) {
		msl_err("cannot write %s: %s", AUTO_MASTER, strerror(errno));
		goto done;
	}

	rc = 1;
	goto done;

overflow:
	msl_err("refusing to rewrite %s: output would not fit", AUTO_MASTER);

done:
	free(out);
	free(text);
	return rc;
}

/* Is our mask marker currently present? */
static bool
is_masked(void)
{
	char *text = msl_slurp(AUTO_MASTER, NULL);
	bool found;

	if (text == NULL)
		return false;

	found = strstr(text, MASK_MARK "/home") != NULL;
	free(text);
	return found;
}

/* Is the auto_home map still mounted on the /home directory? */
static bool
automounted(void)
{
	struct stat sb;

	/*
	 * While autofs owns it the directory is mode 555 and empty; once released
	 * it becomes an ordinary writable directory. Checking writability is more
	 * reliable than parsing mount(8) output, since the mount is nobrowse.
	 */
	if (stat(MSL_HOME_ROOT, &sb) != 0)
		return false;

	return (sb.st_mode & S_IWUSR) == 0;
}

/* Ask the automounter to re-read its configuration and drop released mounts. */
static int
automount_flush(void)
{
	const char *const argv[] = { AUTOMOUNT, "-vc", NULL };
	int rc = msl_run(argv);

	if (rc != 0)
		msl_err("warning: %s -vc exited %d", AUTOMOUNT, rc);

	return rc;
}

/* ---------------------------------------------------------------------------
 * Symlink farm
 * ------------------------------------------------------------------------- */

/*
 * Is `path` a symlink we own - that is, one pointing into /Users? This is the
 * gate on every unlink() below. Anything else in /home is left alone.
 */
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

	return strncmp(target, USERS_ROOT, sizeof(USERS_ROOT) - 1) == 0;
}

/* Create or correct /home/<name> -> /Users/<name>. */
static int
link_user(const struct passwd *pw)
{
	char path[PATH_MAX], target[PATH_MAX];
	struct stat sb;

	snprintf(path, sizeof(path), "%s/%s", MSL_HOME_ROOT, pw->pw_name);

	if (lstat(path, &sb) == 0) {
		if (S_ISLNK(sb.st_mode)) {
			/* Already correct? Then there is nothing to do. */
			if (ours(path, target, sizeof(target)) &&
			    strcmp(target, pw->pw_dir) == 0)
				return 0;

			/* A stale or foreign symlink: replace only if it is ours. */
			if (!ours(path, target, sizeof(target))) {
				msl_err("skipping %s: symlink to %s is not ours", path, target);
				return -1;
			}

			if (unlink(path) != 0) {
				msl_err("cannot replace %s: %s", path, strerror(errno));
				return -1;
			}
		} else {
			/*
			 * A real file or directory. Never delete it - it is far more
			 * likely to be someone's data than a leftover of ours.
			 */
			msl_err("skipping %s: exists and is not a symlink", path);
			return -1;
		}
	}

	if (symlink(pw->pw_dir, path) != 0) {
		msl_err("cannot create %s -> %s: %s", path, pw->pw_dir, strerror(errno));
		return -1;
	}

	msl_log("  linked %s -> %s", path, pw->pw_dir);
	return 0;
}

/*
 * Remove symlinks under /home that we own but that no longer correspond to a
 * live account (deleted users). When `all` is true, remove every symlink we
 * own - the disable path.
 */
static int
prune(bool all)
{
	DIR *dir;
	struct dirent *ent;
	char path[PATH_MAX], target[PATH_MAX];
	int removed = 0;

	dir = opendir(MSL_HOME_ROOT);
	if (dir == NULL)
		return 0;	/* nothing to prune; not an error */

	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", MSL_HOME_ROOT, ent->d_name);

		if (!ours(path, target, sizeof(target)))
			continue;

		if (!all) {
			struct passwd *pw = getpwnam(ent->d_name);
			if (pw != NULL && eligible(pw))
				continue;	/* still a live account */
		}

		if (unlink(path) != 0) {
			msl_err("cannot remove %s: %s", path, strerror(errno));
			continue;
		}

		msl_log("  removed %s", path);
		removed++;
	}

	closedir(dir);
	return removed;
}

/* Populate the farm for every eligible account. */
static int
populate(void)
{
	struct passwd *pw;
	int linked = 0;

	setpwent();
	while ((pw = getpwent()) != NULL) {
		if (!eligible(pw))
			continue;
		if (link_user(pw) == 0)
			linked++;
	}
	endpwent();

	return linked;
}

/* ---------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

int
msl_home_check_safe(char *reason, size_t reason_len)
{
	struct passwd *pw;
	char *text, *line, *next;
	bool network_homes = false;
	char who[256] = "";

	/*
	 * Refuse if any local account's home is *not* under /Users. That is the
	 * signature of network home directories served through the very
	 * automounter map we are about to disable - masking it would break login
	 * for those users.
	 */
	setpwent();
	while ((pw = getpwent()) != NULL) {
		if (!is_login_account(pw))
			continue;
		if (pw->pw_dir != NULL &&
		    strncmp(pw->pw_dir, USERS_ROOT, sizeof(USERS_ROOT) - 1) != 0) {
			network_homes = true;
			snprintf(who, sizeof(who), "%s (home: %s)", pw->pw_name, pw->pw_dir);
			break;
		}
	}
	endpwent();

	if (network_homes) {
		if (reason != NULL)
			snprintf(reason, reason_len,
			    "account %s has a home directory outside /Users, which may be "
			    "served by the auto_home automounter map. Disabling that map "
			    "could prevent that account from logging in.", who);
		return -1;
	}

	/*
	 * Refuse if /etc/auto_home carries entries beyond the two macOS ships,
	 * which would indicate a site-configured automounter we should not touch.
	 */
	text = msl_slurp(AUTO_HOME, NULL);
	if (text != NULL) {
		for (line = text; line != NULL && *line != '\0'; line = next) {
			char *eol = strchr(line, '\n');
			if (eol != NULL) {
				*eol = '\0';
				next = eol + 1;
			} else {
				next = NULL;
			}

			while (*line == ' ' || *line == '\t')
				line++;

			if (*line == '\0' || *line == '#')
				continue;
			if (strncmp(line, "+auto_home", 10) == 0)
				continue;
			if (strncmp(line, "+/usr/libexec/od_user_homes", 27) == 0)
				continue;

			if (reason != NULL)
				snprintf(reason, reason_len,
				    "%s contains a custom entry (\"%s\"). This machine appears "
				    "to use configured automounter home directories.",
				    AUTO_HOME, line);
			free(text);
			return -1;
		}
		free(text);
	}

	return 0;
}

int
msl_home_status(struct msl_home_status *st)
{
	struct passwd *pw;
	DIR *dir;
	struct dirent *ent;
	char path[PATH_MAX], target[PATH_MAX];

	memset(st, 0, sizeof(*st));

	st->enabled     = msl_state_get(MSL_HOME_STATE, 0) != 0;
	st->masked      = is_masked();
	st->automounter = automounted();

	setpwent();
	while ((pw = getpwent()) != NULL) {
		if (eligible(pw))
			st->users++;
	}
	endpwent();

	dir = opendir(MSL_HOME_ROOT);
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
				continue;

			snprintf(path, sizeof(path), "%s/%s", MSL_HOME_ROOT, ent->d_name);

			if (ours(path, target, sizeof(target)))
				st->links++;
			else
				st->foreign++;
		}
		closedir(dir);
	}

	return 0;
}

int
msl_home_enable(void)
{
	char reason[512];
	int rc;

	if (!msl_is_root()) {
		msl_err("enabling /home requires root");
		return -1;
	}

	if (msl_home_check_safe(reason, sizeof(reason)) != 0) {
		msl_err("refusing to enable /home: %s", reason);
		return -1;
	}

	rc = set_masked(true);
	if (rc < 0)
		return -1;

	if (rc == 1) {
		msl_log("masked the /home map in %s", AUTO_MASTER);
		automount_flush();
	}

	/*
	 * The automounter releases the mount asynchronously. If the directory is
	 * still autofs-owned we cannot write into it, and saying so is far more
	 * useful than emitting a symlink error per account.
	 */
	if (automounted()) {
		msl_err("%s is still held by the automounter; "
		    "try again, or reboot if it persists", MSL_HOME_ROOT);
		return -1;
	}

	if (mkdir(MSL_HOME_ROOT, 0755) != 0 && errno != EEXIST) {
		msl_err("cannot create %s: %s", MSL_HOME_ROOT, strerror(errno));
		return -1;
	}

	msl_log("populating %s", MSL_HOME_ROOT);
	populate();
	prune(false);

	if (msl_state_set(MSL_HOME_STATE, 1) != 0)
		msl_err("warning: could not persist state: %s", strerror(errno));

	return 0;
}

int
msl_home_disable(void)
{
	int rc;

	if (!msl_is_root()) {
		msl_err("disabling /home requires root");
		return -1;
	}

	msl_log("removing symlinks from %s", MSL_HOME_ROOT);
	prune(true);

	rc = set_masked(false);
	if (rc < 0)
		return -1;

	if (rc == 1) {
		msl_log("restored the /home map in %s", AUTO_MASTER);
		automount_flush();
	}

	if (msl_state_set(MSL_HOME_STATE, 0) != 0)
		msl_err("warning: could not persist state: %s", strerror(errno));

	return 0;
}

int
msl_home_sync(void)
{
	if (!msl_is_root()) {
		msl_err("syncing /home requires root");
		return -1;
	}

	if (msl_state_get(MSL_HOME_STATE, 0) == 0)
		return 0;	/* component is off; nothing to reconcile */

	if (automounted()) {
		msl_err("%s is held by the automounter; run 'enable' first", MSL_HOME_ROOT);
		return -1;
	}

	populate();
	prune(false);
	return 0;
}
