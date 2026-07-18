/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl.h
 *
 * Shared declarations for the mSL/XNU layout layer: logging, persisted
 * component state, atomic file replacement and subprocess execution.
 */
#ifndef MSL_H
#define MSL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* Persisted per-component state lives here, mirroring procfs's /var/db use. */
#define MSL_STATE_DIR       "/var/db"

/* Local (human) accounts start at this uid; below it are macOS service users. */
#define MSL_MIN_UID         500

/* ---------------------------------------------------------------------------
 * Logging. Diagnostics go to stderr so the LaunchDaemon captures them in its
 * StandardErrorPath, exactly as procfsd does.
 * ------------------------------------------------------------------------- */

void msl_log(const char *fmt, ...) __printflike(1, 2);
void msl_err(const char *fmt, ...) __printflike(1, 2);

/* Suppress msl_log() output (msl_err() is unaffected). For quiet CLI use. */
void msl_set_quiet(bool quiet);

/* ---------------------------------------------------------------------------
 * Persisted state. Each component stores a single integer under MSL_STATE_DIR,
 * e.g. /var/db/msl.home, which the daemon re-applies at boot.
 * ------------------------------------------------------------------------- */

/* Returns the stored value, or `dflt` when the file is absent or unreadable. */
int  msl_state_get(const char *name, int dflt);

/* Returns 0 on success, -1 on failure (errno set). */
int  msl_state_set(const char *name, int value);

/* ---------------------------------------------------------------------------
 * Files.
 * ------------------------------------------------------------------------- */

/*
 * Read an entire file into a NUL-terminated heap buffer. Caller frees.
 * Returns NULL on failure (errno set); *len_out, when non-NULL, receives the
 * length excluding the terminator.
 */
char *msl_slurp(const char *path, size_t *len_out);

/*
 * Replace `path` with `data` atomically: write a sibling temporary file, fsync
 * it, then rename(2) over the target. A crash leaves either the old contents or
 * the new ones, never a truncated file. Ownership is set to root:wheel.
 */
int   msl_write_atomic(const char *path, const char *data, size_t len, mode_t mode);

/* Copy `src` to `dst`, but only if `dst` does not already exist. Used for
 * one-shot backups of system configuration files, so that repeated enable
 * cycles never overwrite the pristine original with an already-modified copy.
 * Returns 0 if the backup was made or already existed, -1 on error. */
int   msl_backup_once(const char *src, const char *dst);

/* ---------------------------------------------------------------------------
 * Subprocesses.
 * ------------------------------------------------------------------------- */

/*
 * Run `argv[0]` with `argv` and wait. Returns the exit status, or -1 if the
 * process could not be spawned or did not exit normally. Output is inherited.
 */
int   msl_run(const char *const argv[]);

/* True when running as uid 0. */
bool  msl_is_root(void);

#endif /* MSL_H */
