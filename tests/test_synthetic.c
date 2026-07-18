/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * test_synthetic.c
 *
 * Exercises the /etc/synthetic.conf rewrite against a copy in a scratch
 * directory. The live file is never read or written.
 *
 * This file is shared with other software - procfs declares its own `proc`
 * entry there, and users add entries by hand - so the property that matters
 * most is that entries we did not write are preserved exactly, through both
 * add and remove.
 *
 * The implementation is #included rather than linked because the helpers are
 * static; its paths are redirected with -D (see the `check` target).
 */
#include "msl_skeleton.c"

static int failures;

static void
check(const char *what, bool ok)
{
	printf("%-52s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

static void
put(const char *content)
{
	if (msl_write_atomic(SYNTHETIC_CONF, content, strlen(content), 0644) != 0) {
		fprintf(stderr, "cannot write %s: %s\n", SYNTHETIC_CONF, strerror(errno));
		exit(2);
	}
}

static char *
get(void)
{
	char *s = msl_slurp(SYNTHETIC_CONF, NULL);
	if (s == NULL) {
		fprintf(stderr, "cannot read %s\n", SYNTHETIC_CONF);
		exit(2);
	}
	return s;
}

static bool
has(const char *needle)
{
	char *s = get();
	bool found = strstr(s, needle) != NULL;
	free(s);
	return found;
}

int
main(void)
{
	struct msl_skeleton_status st;
	char *before, *after;

	msl_set_quiet(true);

	/*
	 * These tests drive the file-rewriting logic directly rather than through
	 * msl_skeleton_add(), which requires root and creates directories on the
	 * Data volume. The parsing and preservation behaviour is what is at issue.
	 */

	/* --- name validation ---------------------------------------------- */
	check("rejects a name with a slash",        !valid_name("foo/bar"));
	check("rejects a name with whitespace",     !valid_name("foo bar"));
	check("rejects an empty name",              !valid_name(""));
	check("rejects NULL",                       !valid_name(NULL));
	check("rejects '..'",                       !valid_name(".."));
	check("rejects '.'",                        !valid_name("."));
	check("accepts a plain name",               valid_name("mnt"));

	/* --- status parsing on a file holding procfs's entry --------------- */
	put("proc\n");
	check("procfs's bare 'proc' entry is seen",
	    msl_skeleton_status("proc", &st) == 0 && st.declared);
	check("a bare-name entry counts as conflicting",     st.conflicting);
	check("an absent name is not declared",
	    msl_skeleton_status("mnt", &st) == 0 && !st.declared);

	/* --- our own entry is recognised ----------------------------------- */
	put("proc\nmnt\t" MSL_DATA_ROOT "/mnt\n");
	check("our entry is declared and not conflicting",
	    msl_skeleton_status("mnt", &st) == 0 && st.declared && !st.conflicting);
	check("the declared target is reported",
	    strcmp(st.target, MSL_DATA_ROOT "/mnt") == 0);

	/* --- an entry pointing elsewhere must be flagged, not adopted ------ */
	put("mnt\t/somewhere/else\n");
	check("a foreign target is flagged as conflicting",
	    msl_skeleton_status("mnt", &st) == 0 && st.declared && st.conflicting);

	/* --- removal preserves everything else ----------------------------- */
	put("proc\nmnt\t" MSL_DATA_ROOT "/mnt\nmedia\t" MSL_DATA_ROOT "/media\n");
	check("remove reports a change",            msl_skeleton_remove("mnt") == 1);
	check("our entry is gone",                  !has("mnt\t"));
	check("procfs's entry survives",            has("proc\n"));
	check("the media entry survives",           has("media\t" MSL_DATA_ROOT "/media"));
	check("remove is idempotent",               msl_skeleton_remove("mnt") == 0);

	/* --- removal must not touch a same-named entry we did not write ---- */
	put("mnt\t/somewhere/else\n");
	before = get();
	check("a foreign entry is not removed",     msl_skeleton_remove("mnt") == 0);
	after = get();
	check("the file is unchanged",              strcmp(before, after) == 0);
	free(before);
	free(after);

	/* --- a bare-name entry of ours is likewise left alone -------------- */
	put("mnt\n");
	check("a bare-name entry is not removed",   msl_skeleton_remove("mnt") == 0);
	check("the bare entry survives",            has("mnt"));

	/* --- comments and blank lines survive a rewrite -------------------- */
	put("# a comment\n\nproc\nmnt\t" MSL_DATA_ROOT "/mnt\n");
	check("remove succeeds with comments present", msl_skeleton_remove("mnt") == 1);
	check("the comment survives",               has("# a comment"));
	check("procfs's entry still survives",      has("proc"));

	printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
