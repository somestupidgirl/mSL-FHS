/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_visibility.h
 *
 * Finder visibility of root-level directories.
 *
 * macOS hides most of the UNIX root entries with the BSD `hidden` file flag
 * (UF_HIDDEN). Cmd-Shift-. reveals flagged items in the Finder, but draws them
 * dimmed; clearing the flag makes them ordinary. `/.hidden`, the mechanism
 * older releases used, no longer exists.
 *
 * Whether the flag can be cleared depends entirely on where the *directory
 * entry* lives, which is not obvious from the path. Measured on macOS 26.5.2:
 *
 *   /opt /cores /Volumes    changeable  - firmlinked to the writable Data volume
 *   /home                   EROFS       - a symlink on the sealed root itself
 *   /private                EPERM       - sunlnk, on the system volume proper
 *   /bin /etc /sbin         EPERM       - SF_RESTRICTED, protected by SIP
 *   /tmp /usr /var
 *   /dev                    silent      - devfs accepts chflags and ignores it
 *
 * The last case is why msl_vis_set() verifies rather than trusting its return
 * value: chflags(2) on /dev reports success and changes nothing, so code that
 * believed it would tell the user a directory had been revealed while the
 * Finder carried on hiding it.
 */
#ifndef MSL_VISIBILITY_H
#define MSL_VISIBILITY_H

#include <stdbool.h>
#include <stddef.h>

/* Why a node's visibility cannot be changed. */
enum msl_vis_lock {
	MSL_VIS_CHANGEABLE = 0,  /* the flag can be set and cleared */
	MSL_VIS_ABSENT,          /* nothing exists at this path */
	MSL_VIS_SIP,             /* SF_RESTRICTED: System Integrity Protection */
	MSL_VIS_READONLY,        /* the entry lives on the sealed system volume */
	MSL_VIS_UNSUPPORTED,     /* the filesystem ignores flag changes (devfs) */
	MSL_VIS_PROTECTED,       /* measured to be refused, cause not introspectable */
};

struct msl_vis_status {
	bool exists;
	bool hidden;             /* UF_HIDDEN is set (there is no SF_HIDDEN) */
	bool symlink;            /* the entry itself is a symlink */
	bool browsable;          /* if a mount point, it is not `nobrowse` */
	bool is_mount;           /* a filesystem is mounted here */
	enum msl_vis_lock lock;
	char fstype[16];         /* filesystem holding the entry */
};

/*
 * The root-level nodes the layer knows about, in the order they are presented.
 * Both the CLI and the GUI iterate this, so the two never drift apart on which
 * directories exist or what they are called.
 */
struct msl_node {
	const char *path;
	bool linux_only;    /* part of the Linux layout, not native to macOS */
};

extern const struct msl_node msl_root_nodes[];
extern const size_t msl_root_node_count;

/* Look up a node by path or bare name ("opt" and "/opt" both work). */
const struct msl_node *msl_node_find(const char *name);

/* Human-readable reason for a lock, or NULL when changeable. */
const char *msl_vis_lock_reason(enum msl_vis_lock lock);

/* Inspect `path`. Returns 0, or -1 only on a malformed argument. */
int msl_vis_status(const char *path, struct msl_vis_status *st);

/*
 * Show or hide `path` in the Finder. Requires root.
 *
 * Returns 0 on success. Returns -1 and fills `reason` (when non-NULL) if the
 * change was refused *or* silently ignored - the flag is re-read afterwards,
 * so a filesystem that accepts the call and does nothing is reported as a
 * failure rather than as success.
 */
int msl_vis_set(const char *path, bool hidden, char *reason, size_t reason_len);

/*
 * Make a mount point browsable, or not, by clearing or setting `nobrowse`.
 *
 * This is a second, independent mechanism from the hidden flag, and /dev needs
 * both: devfs is mounted nobrowse, which removes it from the Finder entirely -
 * Cmd-Shift-. does not reveal it - *and* it carries UF_HIDDEN, which devfs
 * refuses to clear. Clearing nobrowse alone therefore makes /dev appear only
 * when hidden items are being shown, dimmed like any other hidden entry. That
 * is the best available outcome for /dev, and it is worth being clear that it
 * is not the same as fully unhiding it.
 *
 * Requires root. Verifies afterwards, like msl_vis_set().
 */
int msl_vis_set_browsable(const char *path, bool browsable,
                          char *reason, size_t reason_len);

#endif /* MSL_VISIBILITY_H */
