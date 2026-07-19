/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_mnt.h
 *
 * The /mnt component: a directory for manual, temporary mounts, exactly as the
 * FHS defines it.
 *
 * /mnt is scratch space an administrator mounts filesystems into by hand -
 * `mkdir /mnt/disk1 && mount /dev/... /mnt/disk1`. Nothing populates it
 * automatically, so on a stock system it is empty, which is why this component
 * only needs its skeleton entry and gives the daemon nothing to maintain.
 *
 * What the component *does* report is whatever the administrator has mounted
 * there, so the GUI can show it in place of "empty". Detection is read-only:
 * mSL never mounts or unmounts anything under /mnt.
 *
 * Mapping /mnt onto /Volumes would be the intuitive guess and is wrong: it
 * would produce a directory of automatically-appearing volumes that no Linux
 * system has, while duplicating and mis-implementing /media's job.
 */
#ifndef MSL_MNT_H
#define MSL_MNT_H

#include <sys/param.h>

#include "msl_skeleton.h"

#define MSL_MNT_STATE   "msl.mnt"
#define MSL_MNT_NAME    "mnt"

/* A filesystem an administrator has mounted somewhere under /mnt. */
struct msl_mnt_mount {
	char name[128];             /* the mount point's name, e.g. "disk1" */
	char path[MAXPATHLEN];      /* normalised to /mnt/<name> */
	char fstype[16];            /* e.g. "apfs", "msdos", "ntfs" */
};

struct msl_mnt_status {
	bool enabled;                       /* persisted enable flag */
	bool reboot_pending;                /* declared, but not yet live at / */
	struct msl_skeleton_status skel;
	int  mounts;                        /* filesystems mounted under /mnt */
};

int msl_mnt_status(struct msl_mnt_status *st);

/*
 * Enumerate the filesystems mounted directly under /mnt (each /mnt/<name>).
 * Writes up to `max` entries and returns the count, or -1 on error. Read-only.
 *
 * /mnt is a symlink to the Data volume, so a mount there is recorded by the
 * kernel under either path depending on how it was requested; both are matched
 * and reported normalised to /mnt/<name>.
 */
int msl_mnt_scan(struct msl_mnt_mount *out, int max);

/* Declare the skeleton entry. Requires root; takes effect after a reboot. */
int msl_mnt_enable(void);

/* Remove the declaration. Requires root; takes effect after a reboot. */
int msl_mnt_disable(void);

#endif /* MSL_MNT_H */
