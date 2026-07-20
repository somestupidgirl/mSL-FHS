/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs.h
 *
 * Shared declarations for the mSL/XNU layout layer: logging, persisted
 * component state, atomic file replacement and subprocess execution.
 */
#ifndef FHS_H
#define FHS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* Persisted per-component state lives here, mirroring procfs's /var/db use. */
#define FHS_STATE_DIR       "/var/db"

/* Local (human) accounts start at this uid; below it are macOS service users. */
#define FHS_MIN_UID         500

/* ---------------------------------------------------------------------------
 * Logging. Diagnostics go to stderr so the LaunchDaemon captures them in its
 * StandardErrorPath, exactly as procfsd does.
 * ------------------------------------------------------------------------- */

void fhs_log(const char *fmt, ...) __printflike(1, 2);
void fhs_err(const char *fmt, ...) __printflike(1, 2);

/* Suppress fhs_log() output (fhs_err() is unaffected). For quiet CLI use. */
void fhs_set_quiet(bool quiet);

/* ---------------------------------------------------------------------------
 * Persisted state. Each component stores a single integer under FHS_STATE_DIR,
 * e.g. /var/db/fhs.home, which the daemon re-applies at boot.
 * ------------------------------------------------------------------------- */

/* Returns the stored value, or `dflt` when the file is absent or unreadable. */
int  fhs_state_get(const char *name, int dflt);

/* Returns 0 on success, -1 on failure (errno set). */
int  fhs_state_set(const char *name, int value);

/* ---------------------------------------------------------------------------
 * Files.
 * ------------------------------------------------------------------------- */

/*
 * Read an entire file into a NUL-terminated heap buffer. Caller frees.
 * Returns NULL on failure (errno set); *len_out, when non-NULL, receives the
 * length excluding the terminator.
 */
char *fhs_slurp(const char *path, size_t *len_out);

/*
 * Replace `path` with `data` atomically: write a sibling temporary file, fsync
 * it, then rename(2) over the target. A crash leaves either the old contents or
 * the new ones, never a truncated file. Ownership is set to root:wheel.
 */
int   fhs_write_atomic(const char *path, const char *data, size_t len, mode_t mode);

/* Copy `src` to `dst`, but only if `dst` does not already exist. Used for
 * one-shot backups of system configuration files, so that repeated enable
 * cycles never overwrite the pristine original with an already-modified copy.
 * Returns 0 if the backup was made or already existed, -1 on error. */
int   fhs_backup_once(const char *src, const char *dst);

/* ---------------------------------------------------------------------------
 * Subprocesses.
 * ------------------------------------------------------------------------- */

/*
 * Run `argv[0]` with `argv` and wait. Returns the exit status, or -1 if the
 * process could not be spawned or did not exit normally. Output is inherited.
 */
int   fhs_run(const char *const argv[]);

/* True when running as uid 0. */
bool  fhs_is_root(void);

#endif /* FHS_H */
