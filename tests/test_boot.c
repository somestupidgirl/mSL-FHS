/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * test_boot.c
 *
 * The /boot component's two pieces of real logic: selecting the kernel this
 * machine boots, and deciding which symlinks are ours to remove.
 *
 * The second is the one that matters for safety. /boot is meant to hold a real
 * Linux kernel image later, so pruning must never touch anything that is not a
 * link into /System/Library.
 *
 * The implementation is #included to reach its statics.
 */
#include "fhs_boot.c"

#include <fcntl.h>
#include <stdlib.h>

#ifndef TEST_SCRATCH
#define TEST_SCRATCH "/tmp/fhs-boot-test"
#endif

static int failures;

static void
check(const char *what, bool ok)
{
	printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

int
main(void)
{
	char path[MAXPATHLEN], name[64], target[MAXPATHLEN];
	char link[MAXPATHLEN], file[MAXPATHLEN];
	int fd;

	fhs_set_quiet(true);

	/* --- kernel selection ---------------------------------------------- */
	check("the running kernel is found",
	    fhs_boot_running_kernel(path, sizeof(path), name, sizeof(name)));
	check("it is under /System/Library/Kernels",
	    strncmp(path, KERNELS_DIR "/", strlen(KERNELS_DIR) + 1) == 0);
	check("it exists on disk",           access(path, F_OK) == 0);
	check("named darwin-<release>",      strncmp(name, "darwin-", 7) == 0);
	check("the release is not empty",    strlen(name) > 7);

	/*
	 * The name must never be a Linux kernel name. Presenting a Mach-O as
	 * vmlinuz would be exactly the kind of false correspondence this project
	 * avoids elsewhere.
	 */
	check("not named vmlinuz",           strstr(name, "vmlinuz") == NULL);

	/* --- ownership: what pruning may touch ------------------------------ */
	mkdir(TEST_SCRATCH, 0755);

	/* A link into /System/Library is ours. */
	snprintf(link, sizeof(link), "%s/ours", TEST_SCRATCH);
	unlink(link);
	check("created a link into /System/Library",
	    symlink(KERNELS_DIR "/kernel", link) == 0);
	check("a link into /System/Library is ours",
	    ours(link, target, sizeof(target)));
	unlink(link);

	/* A link somewhere else is not. */
	snprintf(link, sizeof(link), "%s/foreign", TEST_SCRATCH);
	unlink(link);
	check("created a link elsewhere",    symlink("/tmp", link) == 0);
	check("a link elsewhere is not ours",
	    !ours(link, target, sizeof(target)));
	unlink(link);

	/*
	 * A regular file is never ours. This is the case that protects a Linux
	 * kernel image dropped into /boot by hand.
	 */
	snprintf(file, sizeof(file), "%s/vmlinuz-6.12.0", TEST_SCRATCH);
	unlink(file);
	fd = open(file, O_CREAT | O_WRONLY, 0644);
	check("created a regular file",      fd >= 0);
	if (fd >= 0)
		close(fd);
	check("a regular file is never ours",
	    !ours(file, target, sizeof(target)));
	unlink(file);

	/* A directory is never ours either. */
	snprintf(link, sizeof(link), "%s/adir", TEST_SCRATCH);
	rmdir(link);
	check("created a directory",         mkdir(link, 0755) == 0);
	check("a directory is never ours",   !ours(link, target, sizeof(target)));
	rmdir(link);

	check("an absent path is not ours",
	    !ours(TEST_SCRATCH "/nothing", target, sizeof(target)));

	rmdir(TEST_SCRATCH);

	printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
