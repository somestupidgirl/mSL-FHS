/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_skeleton.c
 *
 * Management of /etc/synthetic.conf entries. See fhs_skeleton.h for why this
 * mechanism is the only option and why every change costs a reboot.
 *
 * The file is shared: procfs declares its own `proc` entry here, and a user may
 * have added entries by hand. Every rewrite below therefore preserves lines it
 * does not recognise, and removal only ever drops an entry that points exactly
 * where mSL/XNU would have pointed it.
 *
 * Format, per synthetic.conf(5): one entry per line, fields separated by a
 * single tab. A lone name creates an empty directory; a name and an absolute
 * path create a symlink. Comments are not documented as supported, so none are
 * written.
 */
#include "fhs.h"
#include "fhs_skeleton.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SYNTHETIC_CONF
#define SYNTHETIC_CONF  "/etc/synthetic.conf"
#endif

/* ------------------------------------------------------------------------- */

/*
 * A synthetic.conf name must be a single path component: no slashes, no
 * whitespace (the field separator), nothing that would let a caller escape the
 * root directory. Rejecting these here means the rest of the file can assume a
 * well-formed name.
 */
static bool
valid_name(const char *name)
{
	if (name == NULL || name[0] == '\0' || strlen(name) > 64)
		return false;

	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;

	for (const char *p = name; *p != '\0'; p++) {
		if (*p == '/' || isspace((unsigned char)*p))
			return false;
	}

	return true;
}

/*
 * The default target for an entry: a directory of the same name on the writable
 * Data volume. Nodes whose content already exists elsewhere on the system -
 * /root and /run, which are really /var/root and /var/run - pass their target
 * explicitly to the _at() functions instead.
 */
static void
target_for(const char *name, char *buf, size_t bufsz)
{
	snprintf(buf, bufsz, "%s/%s", FHS_DATA_ROOT, name);
}

/*
 * Split a synthetic.conf line into its name and target. `line` is modified in
 * place. Returns false for blank lines. `*target` is NULL for the bare-name
 * (empty directory) form.
 */
static bool
parse_line(char *line, char **name, char **target)
{
	char *p;

	while (*line == ' ' || *line == '\t')
		line++;

	if (*line == '\0' || *line == '#')
		return false;

	*name = line;
	*target = NULL;

	/* The name runs to the first whitespace. */
	for (p = line; *p != '\0'; p++) {
		if (*p == ' ' || *p == '\t') {
			*p++ = '\0';
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p != '\0')
				*target = p;
			break;
		}
	}

	/* Trim trailing whitespace from the target, if any. */
	if (*target != NULL) {
		char *end = *target + strlen(*target);
		while (end > *target && isspace((unsigned char)end[-1]))
			*--end = '\0';
		if (**target == '\0')
			*target = NULL;
	}

	return **name != '\0';
}

/* ------------------------------------------------------------------------- */

int
fhs_skeleton_status_at(const char *name, const char *target,
    struct fhs_skeleton_status *st)
{
	char *text, *line, *next;
	char want[PATH_MAX], root_path[PATH_MAX];
	struct stat sb;

	memset(st, 0, sizeof(*st));

	if (!valid_name(name)) {
		errno = EINVAL;
		return -1;
	}

	snprintf(want, sizeof(want), "%s", target);

	/* Is it declared, and pointing where we expect? */
	text = fhs_slurp(SYNTHETIC_CONF, NULL);
	if (text != NULL) {
		for (line = text; line != NULL && *line != '\0'; line = next) {
			char *ename, *etarget;
			char *eol = strchr(line, '\n');

			if (eol != NULL) {
				*eol = '\0';
				next = eol + 1;
			} else {
				next = NULL;
			}

			if (!parse_line(line, &ename, &etarget))
				continue;
			if (strcmp(ename, name) != 0)
				continue;

			st->declared = true;
			if (etarget != NULL) {
				snprintf(st->target, sizeof(st->target), "%s", etarget);
				st->conflicting = strcmp(etarget, want) != 0;
			} else {
				/* Bare-name form: an empty directory, not our symlink. */
				st->conflicting = true;
			}
			break;
		}
		free(text);
	}

	/* Has the target directory been created? */
	st->target_exists = (stat(want, &sb) == 0 && S_ISDIR(sb.st_mode));

	/*
	 * Is the entry live at /? lstat rather than stat: we want to see the
	 * symlink itself, not what it resolves to.
	 */
	snprintf(root_path, sizeof(root_path), "/%s", name);
	st->active = (lstat(root_path, &sb) == 0);

	return 0;
}

int
fhs_skeleton_add_at(const char *name, const char *target, bool create_target)
{
	struct fhs_skeleton_status st;
	char want[PATH_MAX], line[PATH_MAX];
	char *text = NULL;
	size_t len = 0;
	char *out = NULL;
	size_t outsz;
	int rc = -1;

	if (fhs_skeleton_status_at(name, target, &st) != 0) {
		fhs_err("invalid skeleton name: %s", name ? name : "(null)");
		return -1;
	}

	snprintf(want, sizeof(want), "%s", target);

	if (st.declared && st.conflicting) {
		fhs_err("%s already declares '%s'%s%s - refusing to change it",
		    SYNTHETIC_CONF, name,
		    st.target[0] != '\0' ? " -> " : " as an empty directory",
		    st.target[0] != '\0' ? st.target : "");
		return -1;
	}

	/*
	 * Create the target first, when it is ours to create. If the reboot
	 * happens before it exists, the root symlink would dangle - harmless, but
	 * confusing to anyone looking.
	 *
	 * Nodes pointing at existing system paths (/var/root, /var/run) pass
	 * create_target = false: those directories are macOS's, already present,
	 * and carry their own ownership and modes.
	 */
	if (create_target) {
		if (mkdir(want, 0755) != 0 && errno != EEXIST) {
			fhs_err("cannot create %s: %s", want, strerror(errno));
			return -1;
		}
		if (fhs_is_root() && chown(want, 0, 0) != 0)
			fhs_err("warning: cannot chown %s: %s", want, strerror(errno));
	} else if (access(want, F_OK) != 0) {
		fhs_err("cannot declare /%s: its target %s does not exist",
		    name, want);
		return -1;
	}

	if (st.declared)
		return 0;	/* already correct; target now ensured */

	/* Append our entry, preserving whatever else is in the file. */
	text = fhs_slurp(SYNTHETIC_CONF, &len);
	snprintf(line, sizeof(line), "%s\t%s\n", name, want);

	outsz = len + strlen(line) + 2;
	out = malloc(outsz);
	if (out == NULL)
		goto done;

	if (text != NULL && len > 0) {
		memcpy(out, text, len);
		/* Never join our entry onto an unterminated final line. */
		if (out[len - 1] != '\n')
			out[len++] = '\n';
	}
	memcpy(out + len, line, strlen(line));
	len += strlen(line);

	if (fhs_write_atomic(SYNTHETIC_CONF, out, len, 0644) != 0) {
		fhs_err("cannot write %s: %s", SYNTHETIC_CONF, strerror(errno));
		goto done;
	}

	fhs_log("declared /%s -> %s in %s", name, want, SYNTHETIC_CONF);
	rc = 1;

done:
	free(out);
	free(text);
	return rc;
}

int
fhs_skeleton_remove_at(const char *name, const char *target)
{
	char *text, *out, *line, *next;
	char want[PATH_MAX];
	size_t len, outlen = 0;
	bool changed = false;
	int rc = -1;

	if (!valid_name(name)) {
		errno = EINVAL;
		return -1;
	}

	snprintf(want, sizeof(want), "%s", target);

	text = fhs_slurp(SYNTHETIC_CONF, &len);
	if (text == NULL)
		return 0;	/* no file, nothing declared */

	out = malloc(len + 1);
	if (out == NULL) {
		free(text);
		return -1;
	}

	for (line = text; line != NULL && *line != '\0'; line = next) {
		char *ename, *etarget;
		char *eol = strchr(line, '\n');
		char *original = line;
		char keep[PATH_MAX];

		if (eol != NULL) {
			*eol = '\0';
			next = eol + 1;
		} else {
			next = NULL;
		}

		/* parse_line() edits in place, so keep a copy to re-emit. */
		snprintf(keep, sizeof(keep), "%s", original);

		if (parse_line(line, &ename, &etarget) &&
		    strcmp(ename, name) == 0 &&
		    etarget != NULL && strcmp(etarget, want) == 0) {
			changed = true;
			continue;	/* drop exactly our entry */
		}

		outlen += (size_t)snprintf(out + outlen, len + 1 - outlen, "%s", keep);
		if (eol != NULL && outlen < len)
			out[outlen++] = '\n';
	}

	out[outlen] = '\0';

	if (!changed) {
		rc = 0;
		goto done;
	}

	if (fhs_write_atomic(SYNTHETIC_CONF, out, outlen, 0644) != 0) {
		fhs_err("cannot write %s: %s", SYNTHETIC_CONF, strerror(errno));
		goto done;
	}

	/*
	 * The target directory is deliberately left in place. For /mnt it may hold
	 * whatever the user mounted or created there, and deleting a directory
	 * because a configuration line went away would be a poor trade.
	 */
	fhs_log("removed the /%s declaration from %s (effective after a reboot)",
	    name, SYNTHETIC_CONF);
	rc = 1;

done:
	free(out);
	free(text);
	return rc;
}

bool
fhs_skeleton_reboot_pending_at(const char *name, const char *target)
{
	struct fhs_skeleton_status st;

	if (fhs_skeleton_status_at(name, target, &st) != 0)
		return false;

	return st.declared && !st.conflicting && !st.active;
}

/* ---------------------------------------------------------------------------
 * Name-only wrappers, for the components whose target is the Data-volume
 * directory of the same name.
 * ------------------------------------------------------------------------- */

int
fhs_skeleton_status(const char *name, struct fhs_skeleton_status *st)
{
	char target[PATH_MAX];

	if (!valid_name(name)) {
		errno = EINVAL;
		memset(st, 0, sizeof(*st));
		return -1;
	}

	target_for(name, target, sizeof(target));
	return fhs_skeleton_status_at(name, target, st);
}

int
fhs_skeleton_add(const char *name)
{
	char target[PATH_MAX];

	if (!valid_name(name)) {
		errno = EINVAL;
		return -1;
	}

	target_for(name, target, sizeof(target));
	return fhs_skeleton_add_at(name, target, true);
}

int
fhs_skeleton_remove(const char *name)
{
	char target[PATH_MAX];

	if (!valid_name(name)) {
		errno = EINVAL;
		return -1;
	}

	target_for(name, target, sizeof(target));
	return fhs_skeleton_remove_at(name, target);
}

bool
fhs_skeleton_reboot_pending(const char *name)
{
	char target[PATH_MAX];

	if (!valid_name(name))
		return false;

	target_for(name, target, sizeof(target));
	return fhs_skeleton_reboot_pending_at(name, target);
}
