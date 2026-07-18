/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * test_auto_master.c
 *
 * Exercises the /etc/auto_master mask/unmask rewrite against a copy in a
 * scratch directory, so the live automounter configuration is never touched.
 *
 * The implementation is #included rather than linked because the rewrite
 * helpers are static; the paths they use are overridden by -D at compile time
 * (see the `test` target in the top-level Makefile).
 *
 * What must hold:
 *   - exactly the /home line is masked, and no other line changes
 *   - masking and unmasking are both idempotent (second call reports no change)
 *   - unmask restores the file byte-for-byte
 *   - a file with no /home line is left completely alone
 */
#include "msl_home.c"

static int failures;

static void
check(const char *what, bool ok)
{
	printf("%-46s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

/* Replace the test's auto_master with `content`. */
static void
put(const char *content)
{
	if (msl_write_atomic(AUTO_MASTER, content, strlen(content), 0644) != 0) {
		fprintf(stderr, "cannot write %s: %s\n", AUTO_MASTER, strerror(errno));
		exit(2);
	}
}

static char *
get(void)
{
	char *s = msl_slurp(AUTO_MASTER, NULL);
	if (s == NULL) {
		fprintf(stderr, "cannot read %s\n", AUTO_MASTER);
		exit(2);
	}
	return s;
}

/* The stock macOS auto_master, tabs and all. */
static const char *const STOCK =
    "#\n"
    "# Automounter master map\n"
    "#\n"
    "+auto_master\t\t# Use directory service\n"
    "#/net\t\t\t-hosts\t\t-nobrowse,hidefromfinder,nosuid\n"
    "/home\t\t\tauto_home\t-nobrowse,hidefromfinder\n"
    "/Network/Servers\t-fstab\n"
    "/-\t\t\t-static\n";

int
main(void)
{
	char *original, *masked, *restored;

	msl_set_quiet(true);

	/* --- stock file: mask, unmask, round-trip ------------------------- */
	put(STOCK);
	original = get();

	check("mask reports a change",            set_masked(true) == 1);
	check("mask is idempotent",               set_masked(true) == 0);
	check("is_masked() sees the mark",        is_masked());

	masked = get();
	check("the /home line is commented",      strstr(masked, MASK_MARK "/home") != NULL);
	check("no bare /home line remains",       strstr(masked, "\n/home\t") == NULL);
	check("the /- line is untouched",         strstr(masked, "\n/-\t\t\t-static\n") != NULL);
	check("the /Network line is untouched",   strstr(masked, "\n/Network/Servers\t-fstab\n") != NULL);
	check("the commented /net line is intact",strstr(masked, "\n#/net\t") != NULL);

	check("unmask reports a change",          set_masked(false) == 1);
	check("unmask is idempotent",             set_masked(false) == 0);
	check("is_masked() sees no mark",         !is_masked());

	restored = get();
	check("round-trip is byte-identical",     strcmp(original, restored) == 0);

	free(original);
	free(masked);
	free(restored);

	/* --- a file with no /home line must not be modified ---------------- */
	put("#\n+auto_master\n/-\t\t\t-static\n");
	original = get();
	check("no /home line: mask is a no-op",   set_masked(true) == 0);
	restored = get();
	check("no /home line: file unchanged",    strcmp(original, restored) == 0);
	free(original);
	free(restored);

	/* --- a file missing its trailing newline must not gain or lose one -- */
	put("+auto_master\n/home\tauto_home\t-nobrowse");
	check("no trailing newline: mask works",  set_masked(true) == 1);
	restored = get();
	check("no trailing newline: none added",  restored[strlen(restored) - 1] != '\n');
	free(restored);

	printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
