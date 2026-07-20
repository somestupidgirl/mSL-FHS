/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_media.h
 *
 * The /media component: removable volumes, attributed to a user, as Linux
 * desktops present them.
 *
 *     /media/<user>/<label> -> /Volumes/<label>
 *
 * On Linux this directory holds only removable media mounted by that user's
 * session; internal filesystems mount where fstab says and never appear here.
 * macOS instead puts everything in /Volumes - internal volumes, disk images and
 * network shares included - flat and with no user attribution, so the mapping
 * needs both a filter and an owner. See fhs_media.c for both.
 */
#ifndef FHS_MEDIA_H
#define FHS_MEDIA_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/param.h>

#include "fhs_skeleton.h"

#define FHS_MEDIA_STATE   "fhs.media"
#define FHS_MEDIA_NAME    "media"

/* A mounted volume that passed the admission filter. */
struct fhs_volume {
	char path[MAXPATHLEN];    /* real mount point, e.g. /Volumes/My Disk */
	char label[MAXPATHLEN];   /* sanitised name for use under /media */
};

struct fhs_media_status {
	bool enabled;
	bool reboot_pending;
	struct fhs_skeleton_status skel;
	char user[256];           /* console user, or "" if none */
	int  volumes;             /* removable volumes currently mounted */
	int  links;               /* symlinks currently present under /media */
	int  stale;               /* of those, links whose target is gone */
};

int fhs_media_status(struct fhs_media_status *st);

/* Declare the skeleton entry and sync. Requires root. */
int fhs_media_enable(void);

/* Remove the declaration and all our symlinks. Requires root. */
int fhs_media_disable(void);

/* Reconcile /media with the currently mounted removable volumes. Requires root. */
int fhs_media_sync(void);

/*
 * Enumerate mounted volumes that belong in /media. Writes up to `max` entries
 * and returns the count, or -1 on error. Exposed for the daemon and for tests.
 */
int fhs_media_scan(struct fhs_volume *out, int max);

/*
 * Translate a macOS volume name into a name usable as a Linux path component.
 * Exposed for testing. Returns false if the result would be unusable.
 */
bool fhs_media_sanitise(const char *name, char *out, size_t outsz);

#endif /* FHS_MEDIA_H */
