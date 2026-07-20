/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_detect.h
 *
 * Detection of the pseudo-filesystems that supply the dynamic parts of a Linux
 * tree - /proc (procfs) and /sys (sysfs).
 *
 * These are separate projects with their own kernel extensions, installers and
 * toggles. mSL/FHS reports their state and never changes it: two applications
 * independently mounting and unmounting the same path would race, and a status
 * display that contradicts the actual system is worse than none.
 *
 * Three states are distinguished because each calls for different guidance -
 * absent (install it), installed but unmounted (mount it), and mounted.
 */
#ifndef FHS_DETECT_H
#define FHS_DETECT_H

#include <stdbool.h>

struct fhs_pseudofs {
	const char *name;       /* "procfs" / "sysfs" */
	const char *mountpoint; /* "/proc" / "/sys" */
	bool installed;         /* the filesystem bundle or kext is present */
	bool mounted;           /* it is mounted at `mountpoint` right now */
};

/* Fill in the state of /proc. */
void fhs_detect_procfs(struct fhs_pseudofs *fs);

/* Fill in the state of /sys. */
void fhs_detect_sysfs(struct fhs_pseudofs *fs);

/*
 * Fill in the state of /dev.
 *
 * devfs is built into the kernel rather than installed alongside it, so unlike
 * procfs and sysfs there is no bundle or extension to look for: it is always
 * present, and the only question is whether it is mounted.
 */
void fhs_detect_devfs(struct fhs_pseudofs *fs);

/* Is the mSL layout daemon running? */
bool fhs_daemon_running(void);

#endif /* FHS_DETECT_H */
