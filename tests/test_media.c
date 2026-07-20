/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * test_media.c
 *
 * Label sanitisation and collision handling for /media. These are pure string
 * transformations, so they are tested directly; the DiskArbitration filter and
 * the symlink farm need real devices and are exercised with `fhsctl media list`
 * and `fhsctl media sync`.
 *
 * The implementation is #included to reach its static helpers.
 */
#include "fhs_media.c"

static int failures;

static void
check(const char *what, bool ok)
{
	printf("%-56s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

static void
expect(const char *in, const char *want)
{
	char got[MAXPATHLEN];
	char label[128];

	fhs_media_sanitise(in, got, sizeof(got));
	snprintf(label, sizeof(label), "\"%s\" -> \"%s\"", in ? in : "(null)", want);
	check(label, strcmp(got, want) == 0);
}

int
main(void)
{
	struct fhs_volume vols[4];
	char label[MAXPATHLEN];

	fhs_set_quiet(true);

	/* --- names that pass through unchanged ---------------------------- */
	expect("USBSTICK", "USBSTICK");
	expect("My Passport", "My Passport");     /* spaces are kept, as on Linux */
	expect("backup-2026", "backup-2026");
	expect("Ünïcödé", "Ünïcödé");

	/* --- names needing translation ------------------------------------ */
	expect("a:b", "a_b");                     /* macOS renders '/' as ':' */
	expect("a/b", "a_b");
	expect(".hidden", "_hidden");             /* must not become invisible */
	expect("trailing   ", "trailing");
	expect("with\ttab", "withtab");           /* control characters stripped */

	/* --- degenerate names --------------------------------------------- */
	expect("", "disk");
	expect("   ", "disk");
	expect(NULL, "disk");

	/* --- a name that is only separators still yields something usable -- */
	expect("///", "___");

	/* --- collision handling ------------------------------------------- */
	snprintf(vols[0].label, sizeof(vols[0].label), "UNTITLED");
	snprintf(vols[1].label, sizeof(vols[1].label), "UNTITLED_1");

	snprintf(label, sizeof(label), "UNTITLED");
	deduplicate(vols, 1, label, sizeof(label));
	check("first collision becomes UNTITLED_1", strcmp(label, "UNTITLED_1") == 0);

	snprintf(label, sizeof(label), "UNTITLED");
	deduplicate(vols, 2, label, sizeof(label));
	check("second collision becomes UNTITLED_2", strcmp(label, "UNTITLED_2") == 0);

	snprintf(label, sizeof(label), "OTHER");
	deduplicate(vols, 2, label, sizeof(label));
	check("a non-colliding label is untouched", strcmp(label, "OTHER") == 0);

	printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
