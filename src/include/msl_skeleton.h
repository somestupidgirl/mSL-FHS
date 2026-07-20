/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_skeleton.h
 *
 * Root-level entries created through /etc/synthetic.conf.
 *
 * The macOS root directory is on a sealed, read-only volume, so entries cannot
 * simply be created there. /etc/synthetic.conf is the only supported mechanism,
 * and it is parsed exactly once per boot by apfs_boot_util - which means every
 * change here takes effect only after a restart.
 *
 * mSL/XNU uses the symlink form exclusively:
 *
 *     mnt<TAB>/System/Volumes/Data/mnt
 *
 * The bare-name form creates a directory in the read-only synthesized root,
 * which cannot be written into and is usable only as a mount point. Pointing at
 * the writable Data volume instead gives an ordinary directory whose contents a
 * daemon can maintain with symlink(2) - the same arrangement macOS itself uses
 * for /home.
 */
#ifndef MSL_SKELETON_H
#define MSL_SKELETON_H

#include <stdbool.h>
#include <stddef.h>

/* Where skeleton symlinks point: the root of the writable Data volume. */
#define MSL_DATA_ROOT       "/System/Volumes/Data"

struct msl_skeleton_status {
	bool declared;      /* an entry for this name exists in synthetic.conf */
	bool conflicting;   /* an entry exists but points somewhere else */
	bool target_exists; /* the Data-volume directory has been created */
	bool active;        /* the entry exists at / (i.e. we have rebooted since) */
	char target[256];   /* the declared target, when `declared` */
};

/* Fill `st` for the root-level entry `name` (e.g. "mnt"). Returns 0 or -1. */
int msl_skeleton_status(const char *name, struct msl_skeleton_status *st);

/*
 * Declare a root-level symlink `name` -> MSL_DATA_ROOT/name, and create the
 * target directory. Idempotent.
 *
 * These two do not check for root themselves - the component layer does, so
 * that the rewrite logic can be tested against copies without privilege. They
 * will simply fail with EACCES if called unprivileged against a real system
 * path.
 *
 * Returns 1 if synthetic.conf was changed (a reboot is now needed), 0 if it was
 * already declared, -1 on error.
 */
int msl_skeleton_add(const char *name);

/*
 * Remove our declaration for `name`. Only removes an entry that points where we
 * would have pointed it, so a hand-written entry of the same name is left
 * alone. The target directory is *not* deleted: it may hold data. Idempotent.
 *
 * Returns 1 if synthetic.conf was changed, 0 if there was nothing to remove,
 * -1 on error.
 */
int msl_skeleton_remove(const char *name);

/* True when `name` is declared but not yet present at / - a reboot is pending. */
bool msl_skeleton_reboot_pending(const char *name);

/* ---------------------------------------------------------------------------
 * Explicit-target variants.
 *
 * The functions above assume an entry points at MSL_DATA_ROOT/<name>, which is
 * right for the components that own their contents. Nodes that expose content
 * macOS already has - /root is really /var/root, /run is really /var/run -
 * point elsewhere, and use these.
 *
 * `create_target` says whether the target is ours to create. It must be false
 * for an existing system directory: those belong to macOS, already exist, and
 * carry their own ownership and modes. A declaration whose target is missing is
 * refused rather than left to dangle.
 * ------------------------------------------------------------------------- */

int  msl_skeleton_status_at(const char *name, const char *target,
                            struct msl_skeleton_status *st);
int  msl_skeleton_add_at(const char *name, const char *target,
                         bool create_target);
int  msl_skeleton_remove_at(const char *name, const char *target);
bool msl_skeleton_reboot_pending_at(const char *name, const char *target);

#endif /* MSL_SKELETON_H */
