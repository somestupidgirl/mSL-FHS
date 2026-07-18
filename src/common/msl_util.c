/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_util.c
 *
 * Shared helpers: logging, persisted component state, atomic file replacement
 * and subprocess execution.
 */
#include "msl.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool s_quiet = false;

void
msl_set_quiet(bool quiet)
{
	s_quiet = quiet;
}

void
msl_log(const char *fmt, ...)
{
	va_list ap;

	if (s_quiet)
		return;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void
msl_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("error: ", stderr);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

bool
msl_is_root(void)
{
	return geteuid() == 0;
}

/* ------------------------------------------------------------------------- */

static void
state_path(char *buf, size_t bufsz, const char *name)
{
	snprintf(buf, bufsz, "%s/%s", MSL_STATE_DIR, name);
}

int
msl_state_get(const char *name, int dflt)
{
	char path[PATH_MAX];
	char *text;
	int value;

	state_path(path, sizeof(path), name);

	text = msl_slurp(path, NULL);
	if (text == NULL)
		return dflt;

	/*
	 * Anything that is not a well-formed integer is treated as absent rather
	 * than as zero: a corrupt state file should not silently disable a
	 * component that the user had switched on.
	 */
	errno = 0;
	char *end = NULL;
	long parsed = strtol(text, &end, 10);
	bool ok = (errno == 0 && end != text && parsed >= INT_MIN && parsed <= INT_MAX);
	value = ok ? (int)parsed : dflt;

	free(text);
	return value;
}

int
msl_state_set(const char *name, int value)
{
	char path[PATH_MAX];
	char text[32];
	int len;

	state_path(path, sizeof(path), name);
	len = snprintf(text, sizeof(text), "%d\n", value);

	return msl_write_atomic(path, text, (size_t)len, 0644);
}

/* ------------------------------------------------------------------------- */

char *
msl_slurp(const char *path, size_t *len_out)
{
	int fd;
	struct stat sb;
	char *buf;
	ssize_t got, off = 0;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return NULL;

	if (fstat(fd, &sb) != 0) {
		close(fd);
		return NULL;
	}

	if (!S_ISREG(sb.st_mode)) {
		close(fd);
		errno = EINVAL;
		return NULL;
	}

	buf = malloc((size_t)sb.st_size + 1);
	if (buf == NULL) {
		close(fd);
		return NULL;
	}

	while (off < sb.st_size) {
		got = read(fd, buf + off, (size_t)(sb.st_size - off));
		if (got < 0) {
			if (errno == EINTR)
				continue;
			free(buf);
			close(fd);
			return NULL;
		}
		if (got == 0)
			break;	/* file shrank underneath us */
		off += got;
	}

	buf[off] = '\0';
	close(fd);

	if (len_out != NULL)
		*len_out = (size_t)off;

	return buf;
}

int
msl_write_atomic(const char *path, const char *data, size_t len, mode_t mode)
{
	char tmp[PATH_MAX];
	int fd;
	ssize_t wrote;
	size_t off = 0;
	int saved;

	snprintf(tmp, sizeof(tmp), "%s.msl.tmp", path);

	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
	if (fd < 0)
		return -1;

	while (off < len) {
		wrote = write(fd, data + off, len - off);
		if (wrote < 0) {
			if (errno == EINTR)
				continue;
			goto fail;
		}
		off += (size_t)wrote;
	}

	/* Durability before the rename, so a crash cannot expose an empty file. */
	if (fsync(fd) != 0)
		goto fail;
	if (fchmod(fd, mode) != 0)
		goto fail;
	if (msl_is_root() && fchown(fd, 0, 0) != 0)
		goto fail;
	if (close(fd) != 0) {
		fd = -1;
		goto fail;
	}

	if (rename(tmp, path) != 0) {
		saved = errno;
		unlink(tmp);
		errno = saved;
		return -1;
	}

	return 0;

fail:
	saved = errno;
	if (fd >= 0)
		close(fd);
	unlink(tmp);
	errno = saved;
	return -1;
}

int
msl_backup_once(const char *src, const char *dst)
{
	char *data;
	size_t len;
	int rc;

	if (access(dst, F_OK) == 0)
		return 0;	/* pristine original already saved */

	data = msl_slurp(src, &len);
	if (data == NULL)
		return -1;

	rc = msl_write_atomic(dst, data, len, 0644);
	free(data);
	return rc;
}

/* ------------------------------------------------------------------------- */

int
msl_run(const char *const argv[])
{
	pid_t pid;
	int status;
	int rc;

	rc = posix_spawn(&pid, argv[0], NULL, NULL, (char *const *)argv, NULL);
	if (rc != 0) {
		errno = rc;
		return -1;
	}

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}
