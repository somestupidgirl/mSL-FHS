/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_mnt.c
 *
 * The /mnt component. See msl_mnt.h for why enabling it is only a skeleton
 * entry: on Linux nothing populates /mnt, so the component creates the
 * directory and then leaves it to the administrator.
 *
 * Beyond enable/disable, the component reports what is mounted there, so the
 * GUI can show the actual mounts instead of a bare "empty". That read is the
 * only part that is not a thin pass-through to msl_skeleton.
 */
#include "msl.h"
#include "msl_mnt.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

/* /mnt, and the Data-volume path it is a symlink to. */
#define MNT_ROOT        "/mnt"
#define MNT_DATA_ROOT   MSL_DATA_ROOT "/mnt"

/*
 * If `mp` names a filesystem mounted directly under /mnt, return the mount
 * point's name (the part after /mnt/); otherwise NULL.
 *
 * Both /mnt/<name> and the resolved /System/Volumes/Data/mnt/<name> are
 * accepted, because the kernel records whichever the mount was requested with.
 * Only direct children count - a nested /mnt/a/b is not a /mnt mount point in
 * the sense the administrator means.
 */
static const char *
mnt_child(const char *mp)
{
	const char *rel = NULL;

	if (strncmp(mp, MNT_ROOT "/", sizeof(MNT_ROOT)) == 0)
		rel = mp + sizeof(MNT_ROOT);            /* past "/mnt/" */
	else if (strncmp(mp, MNT_DATA_ROOT "/", sizeof(MNT_DATA_ROOT)) == 0)
		rel = mp + sizeof(MNT_DATA_ROOT);       /* past ".../mnt/" */

	if (rel == NULL || *rel == '\0' || strchr(rel, '/') != NULL)
		return NULL;

	return rel;
}

int
msl_mnt_scan(struct msl_mnt_mount *out, int max)
{
	struct statfs *mounts;
	int n, count = 0;

	n = getmntinfo(&mounts, MNT_NOWAIT);
	if (n <= 0)
		return n == 0 ? 0 : -1;

	for (int i = 0; i < n && count < max; i++) {
		const char *name = mnt_child(mounts[i].f_mntonname);

		if (name == NULL)
			continue;

		snprintf(out[count].name, sizeof(out[count].name), "%s", name);
		snprintf(out[count].path, sizeof(out[count].path), "%s/%s",
		    MNT_ROOT, name);
		snprintf(out[count].fstype, sizeof(out[count].fstype), "%s",
		    mounts[i].f_fstypename);
		count++;
	}

	return count;
}

int
msl_mnt_status(struct msl_mnt_status *st)
{
	struct msl_mnt_mount mounts[64];
	int n;

	memset(st, 0, sizeof(*st));

	st->enabled = msl_state_get(MSL_MNT_STATE, 0) != 0;

	if (msl_skeleton_status(MSL_MNT_NAME, &st->skel) != 0)
		return -1;

	st->reboot_pending = msl_skeleton_reboot_pending(MSL_MNT_NAME);

	n = msl_mnt_scan(mounts, (int)(sizeof(mounts) / sizeof(mounts[0])));
	st->mounts = (n < 0) ? 0 : n;

	return 0;
}

int
msl_mnt_enable(void)
{
	int rc;

	if (!msl_is_root()) {
		msl_err("enabling /mnt requires root");
		return -1;
	}

	rc = msl_skeleton_add(MSL_MNT_NAME);
	if (rc < 0)
		return -1;

	if (msl_state_set(MSL_MNT_STATE, 1) != 0)
		msl_err("warning: could not persist state: %s", strerror(errno));

	if (rc == 1)
		msl_log("/mnt will appear after the next reboot.");
	else if (!msl_skeleton_reboot_pending(MSL_MNT_NAME))
		msl_log("/mnt is already present.");

	return 0;
}

int
msl_mnt_disable(void)
{
	int rc;

	if (!msl_is_root()) {
		msl_err("disabling /mnt requires root");
		return -1;
	}

	rc = msl_skeleton_remove(MSL_MNT_NAME);
	if (rc < 0)
		return -1;

	if (msl_state_set(MSL_MNT_STATE, 0) != 0)
		msl_err("warning: could not persist state: %s", strerror(errno));

	if (rc == 1)
		msl_log("/mnt will disappear after the next reboot.");

	return 0;
}
