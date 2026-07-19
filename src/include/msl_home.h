/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_home.h
 *
 * The /home component: presents Linux-style /home/<user> paths for local
 * accounts whose real home directories live in /Users.
 */
#ifndef MSL_HOME_H
#define MSL_HOME_H

#include <stdbool.h>

#include "msl_skeleton.h"

/* Persisted enable flag: /var/db/msl.home */
#define MSL_HOME_STATE      "msl.home"

/* The root-level entry, declared in /etc/synthetic.conf like /mnt and /media. */
#define MSL_HOME_NAME       "home"

/*
 * Where the symlink farm lives, on the writable Data volume.
 *
 * The root-level /home is *not* a permanent part of macOS: it is created at
 * boot by autofs, from the /home line in /etc/auto_master. Masking that line -
 * which this component must do, because while the map is active it owns the
 * directory and nothing can be created there - therefore removes /home itself
 * at the next boot, leaving the farm below with nothing pointing at it.
 *
 * So the component declares its own /home in /etc/synthetic.conf, exactly as
 * /mnt and /media do. synthetic.conf is processed early at boot, before autofs,
 * and with the map masked autofs does not contend for the name.
 *
 * The consequence is that /home needs a reboot to appear, like the other two.
 * An earlier design relied on the autofs-created /home and appeared to work,
 * because the entry autofs had already made survived for the rest of that boot;
 * it only failed at the next one.
 */
#define MSL_HOME_ROOT       MSL_DATA_ROOT "/home"

struct msl_home_status {
	bool enabled;           /* persisted enable flag */
	bool automounter;       /* the auto_home map still owns /home */
	bool masked;            /* our comment marker is present in auto_master */
	bool reboot_pending;    /* declared in synthetic.conf, not yet live at / */
	int  users;             /* local accounts eligible for a /home entry */
	int  links;             /* symlinks currently present under /home */
	int  foreign;           /* entries under /home that are not ours */
	struct msl_skeleton_status skel;
};

/* Fill `st` with the live state. Returns 0 on success, -1 on error. */
int msl_home_status(struct msl_home_status *st);

/*
 * Refuse-to-proceed check. Returns 0 when it is safe to disable the auto_home
 * automounter map, or -1 with a human-readable reason in `reason` (which may
 * be NULL). Called by msl_home_enable(); exposed so callers can probe first.
 */
int msl_home_check_safe(char *reason, size_t reason_len);

/*
 * Enable: back up and mask the /home line in /etc/auto_master, flush the
 * automounter, then populate the symlink farm. Requires root. Idempotent.
 */
int msl_home_enable(void);

/*
 * Disable: remove only the symlinks this component created, unmask
 * /etc/auto_master and flush the automounter. Requires root. Idempotent.
 */
int msl_home_disable(void);

/*
 * Reconcile the symlink farm with the current account list: add links for new
 * accounts, remove links for deleted ones. Requires root. Safe to call
 * repeatedly; a no-op when the component is disabled.
 */
int msl_home_sync(void);

#endif /* MSL_HOME_H */
