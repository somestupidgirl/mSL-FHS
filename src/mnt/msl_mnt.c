/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_mnt.c
 *
 * The /mnt component. See msl_mnt.h for why this is only a skeleton entry: on
 * Linux /mnt is empty, so faithfully emulating it means creating the directory
 * and then leaving it alone.
 *
 * The whole component is a thin layer over msl_skeleton, kept as its own unit
 * so that the daemon and GUI can treat every component through the same
 * status/enable/disable shape regardless of how much work each one does.
 */
#include "msl.h"
#include "msl_mnt.h"

#include <errno.h>
#include <string.h>

int
msl_mnt_status(struct msl_mnt_status *st)
{
	memset(st, 0, sizeof(*st));

	st->enabled = msl_state_get(MSL_MNT_STATE, 0) != 0;

	if (msl_skeleton_status(MSL_MNT_NAME, &st->skel) != 0)
		return -1;

	st->reboot_pending = msl_skeleton_reboot_pending(MSL_MNT_NAME);
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
