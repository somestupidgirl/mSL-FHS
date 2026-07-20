/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * test_mnt.c
 *
 * Tests the /mnt mount-point matcher. A real mount under /mnt needs root, so
 * the part worth testing without it is mnt_child(): which mount points count as
 * being "under /mnt", under either the /mnt path or the Data-volume path it is
 * a symlink to, and which lookalikes must be rejected.
 *
 * The implementation is #included to reach the static helper.
 */
#include "fhs_mnt.c"

static int failures;

static void
expect(const char *mp, const char *want)
{
	const char *got = mnt_child(mp);
	bool ok = (want == NULL) ? (got == NULL)
	                         : (got != NULL && strcmp(got, want) == 0);

	printf("%-42s -> %-8s %s\n", mp,
	    got ? got : "(none)", ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

int
main(void)
{
	fhs_set_quiet(true);

	/* Direct children under /mnt, both spellings the kernel might record. */
	expect("/mnt/disk1", "disk1");
	expect("/mnt/backup", "backup");
	expect("/System/Volumes/Data/mnt/disk2", "disk2");

	/* /mnt itself is not a child. */
	expect("/mnt", NULL);
	expect("/mnt/", NULL);
	expect("/System/Volumes/Data/mnt", NULL);

	/* Nested mounts are not direct /mnt mount points. */
	expect("/mnt/a/b", NULL);

	/* Lookalike prefixes must not match. */
	expect("/mntfoo/bar", NULL);
	expect("/mnt2/disk", NULL);
	expect("/Users/sunneva", NULL);
	expect("/", NULL);

	printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
