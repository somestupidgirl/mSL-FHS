/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_mnt.h
 *
 * The /mnt component: an empty directory, which is what /mnt is on Linux.
 *
 * The FHS defines /mnt as a mount point for a temporarily mounted filesystem -
 * scratch space an administrator uses by hand. Nothing populates it, so on a
 * stock Linux system it is empty. This component therefore consists entirely of
 * its skeleton entry; there are no contents to maintain and nothing for the
 * daemon to do.
 *
 * Mapping /mnt onto /Volumes would be the intuitive guess and is wrong: it
 * would produce a directory of automatically-appearing volumes that no Linux
 * system has, while duplicating and mis-implementing /media's job.
 */
#ifndef MSL_MNT_H
#define MSL_MNT_H

#include "msl_skeleton.h"

#define MSL_MNT_STATE   "msl.mnt"
#define MSL_MNT_NAME    "mnt"

struct msl_mnt_status {
	bool enabled;                       /* persisted enable flag */
	bool reboot_pending;                /* declared, but not yet live at / */
	struct msl_skeleton_status skel;
};

int msl_mnt_status(struct msl_mnt_status *st);

/* Declare the skeleton entry. Requires root; takes effect after a reboot. */
int msl_mnt_enable(void);

/* Remove the declaration. Requires root; takes effect after a reboot. */
int msl_mnt_disable(void);

#endif /* MSL_MNT_H */
