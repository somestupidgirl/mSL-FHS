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

/* Persisted enable flag: /var/db/msl.home */
#define MSL_HOME_STATE      "msl.home"

/*
 * macOS ships /home as a symlink to this directory on the writable Data
 * volume, so the component needs no /etc/synthetic.conf entry and no reboot.
 */
#define MSL_HOME_ROOT       "/System/Volumes/Data/home"

struct msl_home_status {
	bool enabled;           /* persisted enable flag */
	bool automounter;       /* the auto_home map still owns /home */
	bool masked;            /* our comment marker is present in auto_master */
	int  users;             /* local accounts eligible for a /home entry */
	int  links;             /* symlinks currently present under /home */
	int  foreign;           /* entries under /home that are not ours */
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
