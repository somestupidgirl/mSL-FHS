/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * mslctl.c
 *
 * Command-line control for the mSL/XNU layout layer. This is the mechanism the
 * daemon and GUI will drive; having it as a tool first means every component
 * can be exercised and verified before either of them exists.
 *
 *     mslctl status              show every component
 *     mslctl home status         show the /home component
 *     mslctl home enable         requires root
 *     mslctl home disable        requires root
 *     mslctl home sync           requires root
 */
#include "msl.h"
#include "msl_home.h"

#include <stdio.h>
#include <string.h>

static int
home_status(void)
{
	struct msl_home_status st;

	if (msl_home_status(&st) != 0) {
		msl_err("cannot read /home status");
		return 1;
	}

	printf("/home\n");
	printf("  state        %s\n", st.enabled ? "enabled" : "disabled");
	printf("  automounter  %s\n",
	    st.automounter ? "active (auto_home owns /home)" : "released");
	printf("  auto_master  %s\n", st.masked ? "masked by mSL" : "unmodified");
	printf("  accounts     %d eligible\n", st.users);
	printf("  links        %d\n", st.links);

	if (st.foreign > 0)
		printf("  foreign      %d entry(s) not created by mSL (left untouched)\n",
		    st.foreign);

	/*
	 * Report the states that are internally inconsistent, since they are what
	 * a user is most likely to need help with.
	 */
	if (st.enabled && st.automounter)
		printf("\n  note: enabled, but the automounter still owns %s.\n"
		       "        Run 'sudo mslctl home enable' again, or reboot.\n",
		       MSL_HOME_ROOT);
	else if (st.enabled && st.links < st.users)
		printf("\n  note: %d account(s) have no /home entry. "
		       "Run 'sudo mslctl home sync'.\n", st.users - st.links);

	return 0;
}

static int
home_command(const char *verb)
{
	if (verb == NULL || strcmp(verb, "status") == 0)
		return home_status();

	if (strcmp(verb, "enable") == 0)
		return msl_home_enable() == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return msl_home_disable() == 0 ? 0 : 1;

	if (strcmp(verb, "sync") == 0)
		return msl_home_sync() == 0 ? 0 : 1;

	if (strcmp(verb, "check") == 0) {
		char reason[512];
		if (msl_home_check_safe(reason, sizeof(reason)) != 0) {
			printf("unsafe: %s\n", reason);
			return 1;
		}
		printf("safe: no automounter home directories detected\n");
		return 0;
	}

	msl_err("unknown command: home %s", verb);
	return 2;
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mslctl <component> <command>\n"
	    "\n"
	    "components:\n"
	    "  home     Linux-style /home/<user> paths for local accounts\n"
	    "\n"
	    "commands:\n"
	    "  status   show the current state (default)\n"
	    "  check    report whether the component is safe to enable\n"
	    "  enable   turn the component on           (root)\n"
	    "  disable  turn the component off          (root)\n"
	    "  sync     reconcile with the account list (root)\n");
}

int
main(int argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		usage();
		return argc < 2 ? 2 : 0;
	}

	if (strcmp(argv[1], "status") == 0)
		return home_status();

	if (strcmp(argv[1], "home") == 0)
		return home_command(argc > 2 ? argv[2] : NULL);

	msl_err("unknown component: %s", argv[1]);
	usage();
	return 2;
}
