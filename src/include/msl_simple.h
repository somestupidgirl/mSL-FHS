/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_simple.h
 *
 * The nodes that are nothing but a skeleton entry: /root, /run and /srv.
 *
 * /home, /mnt and /media each have real work behind them - an automounter to
 * mask, mounts to report, volumes to track - and are their own components. The
 * three here differ only in a name and a target, so they share one
 * table-driven implementation rather than three near-identical copies.
 *
 * Two of them expose content macOS already has, under the name Linux uses:
 *
 *   /root -> /var/root    the superuser's home directory, which is exactly
 *                         what /root is on Linux
 *   /run  -> /var/run     runtime state - pid files, sockets - which is
 *                         exactly what /run is on Linux
 *
 * Those targets are macOS's own directories: the entry is a new name for
 * existing content, not a new place to put things. /srv has no macOS
 * equivalent and is an empty directory of our own, which is what /srv is on
 * most Linux systems too.
 */
#ifndef MSL_SIMPLE_H
#define MSL_SIMPLE_H

#include <stdbool.h>
#include <stddef.h>

#include "msl_skeleton.h"

struct msl_simple_def {
	const char *name;       /* "root" */
	const char *target;     /* "/var/root" */
	const char *state;      /* persisted flag, "msl.root" */
	const char *summary;    /* one-line description for the interface */
	bool creates_target;    /* false when the target is macOS's own directory */
};

extern const struct msl_simple_def msl_simple_nodes[];
extern const size_t msl_simple_node_count;

struct msl_simple_status {
	bool enabled;
	bool reboot_pending;
	bool target_missing;    /* the target went away underneath us */
	struct msl_skeleton_status skel;
};

/* Look up by name or path ("run" and "/run" both work). NULL if not one. */
const struct msl_simple_def *msl_simple_find(const char *name);

int msl_simple_status(const struct msl_simple_def *def,
                      struct msl_simple_status *st);

/* Declare the entry. Requires root; takes effect after a reboot. */
int msl_simple_enable(const struct msl_simple_def *def);

/* Withdraw the entry. Requires root; takes effect after a reboot. */
int msl_simple_disable(const struct msl_simple_def *def);

#endif /* MSL_SIMPLE_H */
