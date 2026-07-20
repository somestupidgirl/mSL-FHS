/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * test_simple.c
 *
 * The skeleton-only nodes: the table, the lookup, and the property that makes
 * them safe - that a node pointing at a macOS directory never claims to create
 * it.
 *
 * Enabling needs root and a reboot to observe, so what is tested here is the
 * definition table and the lookup, plus a read-only status of each node against
 * the real system.
 *
 * The implementation is #included to reach its statics.
 */
#include "fhs_simple.c"

#include <stdlib.h>

static int failures;

static void
check(const char *what, bool ok)
{
	printf("%-56s %s\n", what, ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

int
main(void)
{
	struct fhs_simple_status st;

	fhs_set_quiet(true);

	/* --- lookup -------------------------------------------------------- */
	check("finds root by name",          fhs_simple_find("root") != NULL);
	check("finds root by path",          fhs_simple_find("/root") != NULL);
	check("name and path agree",         fhs_simple_find("root") == fhs_simple_find("/root"));
	check("finds run",                   fhs_simple_find("run") != NULL);
	check("finds srv",                   fhs_simple_find("srv") != NULL);
	check("rejects an unknown name",     fhs_simple_find("nonesuch") == NULL);
	check("rejects empty",               fhs_simple_find("") == NULL);
	check("rejects NULL",                fhs_simple_find(NULL) == NULL);

	/* The components with real work behind them are not simple nodes. */
	check("home is not a simple node",   fhs_simple_find("home") == NULL);
	check("mnt is not a simple node",    fhs_simple_find("mnt") == NULL);
	check("media is not a simple node",  fhs_simple_find("media") == NULL);

	/* --- the table ----------------------------------------------------- */
	check("three nodes defined",         fhs_simple_node_count == 3);

	for (size_t i = 0; i < fhs_simple_node_count; i++) {
		const struct fhs_simple_def *d = &fhs_simple_nodes[i];
		char desc[128];

		snprintf(desc, sizeof(desc), "/%s has a target", d->name);
		check(desc, d->target != NULL && d->target[0] == '/');

		snprintf(desc, sizeof(desc), "/%s has a state file name", d->name);
		check(desc, d->state != NULL && strncmp(d->state, "fhs.", 4) == 0);

		snprintf(desc, sizeof(desc), "/%s has a summary", d->name);
		check(desc, d->summary != NULL && d->summary[0] != '\0');

		/*
		 * The safety property. A node whose target is a macOS directory must
		 * not be marked as creating it: mkdir-ing over /var/root or /var/run
		 * would be this layer taking ownership of something that is not its
		 * own. Only targets on our own Data-volume path may be created.
		 */
		snprintf(desc, sizeof(desc),
		    "/%s creates its target only if it is ours", d->name);
		check(desc, !d->creates_target ||
		    strncmp(d->target, FHS_DATA_ROOT "/", sizeof(FHS_DATA_ROOT)) == 0);
	}

	/* --- the specific mappings, which are the point of the component ---- */
	check("/root points at /var/root",
	    strcmp(fhs_simple_find("root")->target, "/var/root") == 0);
	check("/run points at /var/run",
	    strcmp(fhs_simple_find("run")->target, "/var/run") == 0);
	check("/root does not create its target",
	    !fhs_simple_find("root")->creates_target);
	check("/run does not create its target",
	    !fhs_simple_find("run")->creates_target);
	check("/srv does create its target",
	    fhs_simple_find("srv")->creates_target);

	/* --- status against the real system, read-only ---------------------- */
	check("status of /root reads",       fhs_simple_status(fhs_simple_find("root"), &st) == 0);
	check("/var/root exists, so /root's target is present", !st.target_missing);
	check("status of /run reads",        fhs_simple_status(fhs_simple_find("run"), &st) == 0);
	check("/var/run exists, so /run's target is present",   !st.target_missing);

	check("status rejects NULL def",     fhs_simple_status(NULL, &st) == -1);

	printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
