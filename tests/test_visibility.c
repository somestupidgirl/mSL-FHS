/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * test_visibility.c
 *
 * Two kinds of test, deliberately separated.
 *
 * The mutation tests run entirely inside a scratch directory on files this
 * process owns. UF_HIDDEN is a user flag, so its owner can set and clear it
 * without privilege - which means the full set/verify cycle is testable
 * without root and without touching a single system path.
 *
 * The classification tests read real root-level directories, asserting that
 * each is categorised the way it actually behaves on this machine. They are
 * read-only: nothing here attempts to change a system path. They encode the
 * measurements the module was built from, so that an OS update which changes
 * one of them is caught here rather than by a user finding a toggle that no
 * longer works.
 *
 * The implementation is #included to reach its static helpers.
 */
#include "fhs_visibility.c"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef TEST_SCRATCH
#define TEST_SCRATCH "/tmp/fhs-vis-test"
#endif

static int failures;
static int checks;

static void
check(const char *what, bool ok)
{
	checks++;
	printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

static void
check_lock(const char *path, enum fhs_vis_lock want, const char *label)
{
	struct fhs_vis_status st;
	char desc[160];

	if (fhs_vis_status(path, &st) != 0) {
		snprintf(desc, sizeof(desc), "%s: status readable", path);
		check(desc, false);
		return;
	}

	snprintf(desc, sizeof(desc), "%-14s classified as %s", path, label);
	check(desc, st.lock == want);
}

static void
scratch_path(char *buf, size_t n, const char *name)
{
	snprintf(buf, n, "%s/%s", TEST_SCRATCH, name);
}

int
main(void)
{
	struct fhs_vis_status st;
	char dir[512], link[512], target[512], reason[512];
	int fd;

	fhs_set_quiet(true);

	/* --- node table ---------------------------------------------------- */
	check("finds a node by full path",       fhs_node_find("/opt") != NULL);
	check("finds a node by bare name",       fhs_node_find("opt") != NULL);
	check("bare and full name agree",        fhs_node_find("opt") == fhs_node_find("/opt"));
	check("rejects an unknown node",         fhs_node_find("/nonesuch") == NULL);
	check("rejects an empty name",           fhs_node_find("") == NULL);
	check("rejects NULL",                    fhs_node_find(NULL) == NULL);
	check("a partial name does not match",   fhs_node_find("op") == NULL);
	check("/dev is in the table",            fhs_node_find("/dev") != NULL);
	check("Linux-only nodes are marked",     fhs_node_find("/media")->linux_only);
	check("native nodes are not",            !fhs_node_find("/usr")->linux_only);

	/* Every lock value must have a reason; only CHANGEABLE has none. */
	check("CHANGEABLE has no reason",   fhs_vis_lock_reason(FHS_VIS_CHANGEABLE) == NULL);
	check("ABSENT has a reason",        fhs_vis_lock_reason(FHS_VIS_ABSENT) != NULL);
	check("SIP has a reason",           fhs_vis_lock_reason(FHS_VIS_SIP) != NULL);
	check("READONLY has a reason",      fhs_vis_lock_reason(FHS_VIS_READONLY) != NULL);
	check("UNSUPPORTED has a reason",   fhs_vis_lock_reason(FHS_VIS_UNSUPPORTED) != NULL);
	check("PROTECTED has a reason",     fhs_vis_lock_reason(FHS_VIS_PROTECTED) != NULL);

	/* --- mutation, on files we own ------------------------------------- */
	mkdir(TEST_SCRATCH, 0755);

	scratch_path(dir, sizeof(dir), "adir");
	rmdir(dir);
	check("scratch directory created",       mkdir(dir, 0755) == 0);

	check("a new directory is not hidden",
	    fhs_vis_status(dir, &st) == 0 && st.exists && !st.hidden);
	check("a plain directory is changeable", st.lock == FHS_VIS_CHANGEABLE);
	check("a plain directory is not a symlink", !st.symlink);

	check("hiding succeeds",                 fhs_vis_set(dir, true, reason, sizeof(reason)) == 0);
	check("it now reads as hidden",
	    fhs_vis_status(dir, &st) == 0 && st.hidden);
	check("hiding again is a no-op",         fhs_vis_set(dir, true, reason, sizeof(reason)) == 0);

	check("showing succeeds",                fhs_vis_set(dir, false, reason, sizeof(reason)) == 0);
	check("it now reads as shown",
	    fhs_vis_status(dir, &st) == 0 && !st.hidden);
	check("showing again is a no-op",        fhs_vis_set(dir, false, reason, sizeof(reason)) == 0);

	/* Other flags must survive a visibility change. */
	check("set an unrelated flag",           lchflags(dir, UF_NODUMP) == 0);
	check("hiding preserves it",
	    fhs_vis_set(dir, true, reason, sizeof(reason)) == 0);
	{
		struct stat sb;
		check("UF_NODUMP still set",
		    lstat(dir, &sb) == 0 && (sb.st_flags & UF_NODUMP) != 0);
		check("UF_HIDDEN also set",
		    (sb.st_flags & UF_HIDDEN) != 0);
	}
	lchflags(dir, 0);

	/* --- symlinks: the link is the subject, never its target ----------- */
	scratch_path(link, sizeof(link), "alink");
	scratch_path(target, sizeof(target), "atarget");
	unlink(link);
	unlink(target);

	fd = open(target, O_CREAT | O_WRONLY, 0644);
	check("target file created",             fd >= 0);
	if (fd >= 0)
		close(fd);

	check("symlink created",                 symlink(target, link) == 0);
	check("symlink is seen as a symlink",
	    fhs_vis_status(link, &st) == 0 && st.symlink);

	check("hiding the symlink succeeds",     fhs_vis_set(link, true, reason, sizeof(reason)) == 0);
	check("the symlink is hidden",
	    fhs_vis_status(link, &st) == 0 && st.hidden);
	check("the target is untouched",
	    fhs_vis_status(target, &st) == 0 && !st.hidden);

	/* --- absent paths --------------------------------------------------- */
	check("an absent path reports ABSENT",
	    fhs_vis_status(TEST_SCRATCH "/nothing", &st) == 0 &&
	    !st.exists && st.lock == FHS_VIS_ABSENT);
	check("setting an absent path fails",
	    fhs_vis_set(TEST_SCRATCH "/nothing", true, reason, sizeof(reason)) == -1);
	check("and says so",                     strstr(reason, "does not exist") != NULL);

	check("NULL path is rejected",           fhs_vis_status(NULL, &st) == -1);

	unlink(link);
	unlink(target);
	rmdir(dir);
	rmdir(TEST_SCRATCH);

	/* --- classification of the real root, read-only --------------------- */
	printf("\n  (the following read real system paths; none are modified)\n");

	check_lock("/bin",     FHS_VIS_SIP,         "SIP");
	check_lock("/usr",     FHS_VIS_SIP,         "SIP");
	check_lock("/var",     FHS_VIS_SIP,         "SIP");
	/*
	 * /home is conditional. It exists only while something has created the
	 * root-level entry - autofs, or our own synthetic.conf declaration after a
	 * reboot - so it is either absent, or present and locked read-only because
	 * a symlink at / lives on the sealed root. Asserting a single one of those
	 * pins a state the system is entitled to change.
	 */
	if (fhs_vis_status("/home", &st) == 0 && st.exists)
		check_lock("/home", FHS_VIS_READONLY, "read-only");
	else
		check_lock("/home", FHS_VIS_ABSENT, "absent");
	check_lock("/private", FHS_VIS_PROTECTED,   "protected");
	check_lock("/dev",     FHS_VIS_UNSUPPORTED, "unsupported");
	check_lock("/opt",     FHS_VIS_CHANGEABLE,  "changeable");
	check_lock("/cores",   FHS_VIS_CHANGEABLE,  "changeable");
	check_lock("/Volumes", FHS_VIS_CHANGEABLE,  "changeable");

	/* /dev is the case the verification logic exists for. */
	check("/dev is a mount point",
	    fhs_vis_status("/dev", &st) == 0 && st.is_mount);
	check("/dev is mounted nobrowse",        !st.browsable);
	check("/dev is hidden",                  st.hidden);
	check("changing /dev is refused up front",
	    fhs_vis_set("/dev", false, reason, sizeof(reason)) == -1);
	check("and names the filesystem as the cause",
	    strstr(reason, "ignores visibility flags") != NULL);

	/*
	 * A system path we do not own must be refused for the right reason.
	 *
	 * The request has to be a real change, because asking for the state a node
	 * is already in returns early as a no-op and never reaches the ownership
	 * check. So invert whatever /opt currently is rather than assuming - an
	 * earlier version hard-coded "hidden" and started failing the moment the
	 * node was legitimately shown, which is the test being wrong and not the
	 * system.
	 */
	if (geteuid() != 0 && fhs_vis_status("/opt", &st) == 0 && st.exists) {
		check("an unowned node reports the ownership requirement",
		    fhs_vis_set("/opt", !st.hidden, reason, sizeof(reason)) == -1 &&
		    strstr(reason, "requires root") != NULL);
	}

	printf("\n%d checks, %s\n", checks,
	    failures == 0 ? "all passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
