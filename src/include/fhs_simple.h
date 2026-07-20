/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_simple.h
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
#ifndef FHS_SIMPLE_H
#define FHS_SIMPLE_H

#include <stdbool.h>
#include <stddef.h>

#include "fhs_skeleton.h"

struct fhs_simple_def {
	const char *name;       /* "root" */
	const char *target;     /* "/var/root" */
	const char *state;      /* persisted flag, "fhs.root" */
	const char *summary;    /* one-line description for the interface */
	bool creates_target;    /* false when the target is macOS's own directory */
};

extern const struct fhs_simple_def fhs_simple_nodes[];
extern const size_t fhs_simple_node_count;

struct fhs_simple_status {
	bool enabled;
	bool reboot_pending;
	bool target_missing;    /* the target went away underneath us */
	struct fhs_skeleton_status skel;
};

/* Look up by name or path ("run" and "/run" both work). NULL if not one. */
const struct fhs_simple_def *fhs_simple_find(const char *name);

int fhs_simple_status(const struct fhs_simple_def *def,
                      struct fhs_simple_status *st);

/* Declare the entry. Requires root; takes effect after a reboot. */
int fhs_simple_enable(const struct fhs_simple_def *def);

/* Withdraw the entry. Requires root; takes effect after a reboot. */
int fhs_simple_disable(const struct fhs_simple_def *def);

#endif /* FHS_SIMPLE_H */
