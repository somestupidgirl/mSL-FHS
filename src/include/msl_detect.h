/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_detect.h
 *
 * Detection of the pseudo-filesystems that supply the dynamic parts of a Linux
 * tree - /proc (procfs) and /sys (sysfs).
 *
 * These are separate projects with their own kernel extensions, installers and
 * toggles. mSL/XNU reports their state and never changes it: two applications
 * independently mounting and unmounting the same path would race, and a status
 * display that contradicts the actual system is worse than none.
 *
 * Three states are distinguished because each calls for different guidance -
 * absent (install it), installed but unmounted (mount it), and mounted.
 */
#ifndef MSL_DETECT_H
#define MSL_DETECT_H

#include <stdbool.h>

struct msl_pseudofs {
	const char *name;       /* "procfs" / "sysfs" */
	const char *mountpoint; /* "/proc" / "/sys" */
	bool installed;         /* the filesystem bundle or kext is present */
	bool mounted;           /* it is mounted at `mountpoint` right now */
};

/* Fill in the state of /proc. */
void msl_detect_procfs(struct msl_pseudofs *fs);

/* Fill in the state of /sys. */
void msl_detect_sysfs(struct msl_pseudofs *fs);

/* Is the mSL layout daemon running? */
bool msl_daemon_running(void);

#endif /* MSL_DETECT_H */
