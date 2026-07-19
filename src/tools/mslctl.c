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
#include "msl_detect.h"
#include "msl_home.h"
#include "msl_media.h"
#include "msl_mnt.h"
#include "msl_visibility.h"

#include <stdio.h>
#include <string.h>

/* One-line summary of a pseudo-filesystem we only observe. */
static const char *
describe(const struct msl_pseudofs *fs)
{
	if (fs->mounted)
		return "mounted";
	if (fs->installed)
		return "installed, not mounted";
	return "not installed";
}

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

/*
 * Machine-readable state, one `key=value` per line.
 *
 * The GUI reads this rather than parsing the human-readable output above:
 * status text is written to be read by people and will be reworded, whereas
 * these keys are an interface. Keeping both in one tool means the GUI and the
 * CLI can never report different things.
 */
static int
porcelain(void)
{
	struct msl_home_status home;
	struct msl_mnt_status mnt;
	struct msl_media_status media;
	struct msl_pseudofs proc, sys;

	if (msl_home_status(&home) == 0) {
		printf("home.enabled=%d\n", home.enabled);
		printf("home.automounter=%d\n", home.automounter);
		printf("home.masked=%d\n", home.masked);
		printf("home.users=%d\n", home.users);
		printf("home.links=%d\n", home.links);
		printf("home.foreign=%d\n", home.foreign);
	}

	if (msl_mnt_status(&mnt) == 0) {
		printf("mnt.enabled=%d\n", mnt.enabled);
		printf("mnt.declared=%d\n", mnt.skel.declared);
		printf("mnt.conflicting=%d\n", mnt.skel.conflicting);
		printf("mnt.active=%d\n", mnt.skel.active);
		printf("mnt.reboot_pending=%d\n", mnt.reboot_pending);
	}

	if (msl_media_status(&media) == 0) {
		printf("media.enabled=%d\n", media.enabled);
		printf("media.declared=%d\n", media.skel.declared);
		printf("media.conflicting=%d\n", media.skel.conflicting);
		printf("media.active=%d\n", media.skel.active);
		printf("media.reboot_pending=%d\n", media.reboot_pending);
		printf("media.user=%s\n", media.user);
		printf("media.volumes=%d\n", media.volumes);
		printf("media.links=%d\n", media.links);
		printf("media.stale=%d\n", media.stale);
	}

	msl_detect_procfs(&proc);
	msl_detect_sysfs(&sys);
	printf("proc.installed=%d\n", proc.installed);
	printf("proc.mounted=%d\n", proc.mounted);
	printf("sys.installed=%d\n", sys.installed);
	printf("sys.mounted=%d\n", sys.mounted);

	printf("daemon.running=%d\n", msl_daemon_running());
	printf("version=%s\n", MSL_VERSION);

	return 0;
}

static int
media_status(void)
{
	struct msl_media_status st;

	if (msl_media_status(&st) != 0) {
		msl_err("cannot read /media status");
		return 1;
	}

	printf("/media\n");
	printf("  state        %s\n", st.enabled ? "enabled" : "disabled");
	printf("  declared     %s\n",
	    st.skel.declared ? "yes, in /etc/synthetic.conf" : "no");
	printf("  present      %s\n", st.skel.active ? "yes, /media exists" : "no");
	printf("  console user %s\n", st.user[0] != '\0' ? st.user : "(none logged in)");
	printf("  volumes      %d removable mounted\n", st.volumes);
	printf("  links        %d%s", st.links, st.stale > 0 ? "" : "\n");
	if (st.stale > 0)
		printf(" (%d stale)\n", st.stale);

	if (st.stale > 0)
		printf("\n  note: %d link(s) point at volumes that are no longer mounted.\n"
		       "        Run 'sudo mslctl media sync' to clear them. Until mslxd\n"
		       "        watches for ejects, this is expected after unmounting.\n",
		       st.stale);

	if (st.skel.conflicting)
		printf("\n  note: /etc/synthetic.conf declares 'media' but not as ours.\n"
		       "        mSL will not modify it.\n");
	else if (st.reboot_pending)
		printf("\n  note: declared, but /media appears only after a reboot.\n"
		       "        The symlinks below it are already in place.\n");
	else if (st.enabled && st.user[0] == '\0')
		printf("\n  note: no graphical session, so there is no user to attribute\n"
		       "        mounts to. Existing links are left alone.\n");

	return 0;
}

static int
media_command(const char *verb)
{
	if (verb == NULL || strcmp(verb, "status") == 0)
		return media_status();

	if (strcmp(verb, "enable") == 0)
		return msl_media_enable() == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return msl_media_disable() == 0 ? 0 : 1;

	if (strcmp(verb, "sync") == 0)
		return msl_media_sync() == 0 ? 0 : 1;

	if (strcmp(verb, "list") == 0) {
		struct msl_volume vols[64];
		int n = msl_media_scan(vols, 64);

		if (n < 0) {
			msl_err("cannot enumerate volumes");
			return 1;
		}
		if (n == 0) {
			printf("no removable volumes mounted\n");
			return 0;
		}
		for (int i = 0; i < n; i++)
			printf("%-24s -> %s\n", vols[i].label, vols[i].path);
		return 0;
	}

	msl_err("unknown command: media %s", verb);
	return 2;
}

static int
mnt_status(void)
{
	struct msl_mnt_status st;

	if (msl_mnt_status(&st) != 0) {
		msl_err("cannot read /mnt status");
		return 1;
	}

	printf("/mnt\n");
	printf("  state        %s\n", st.enabled ? "enabled" : "disabled");
	printf("  declared     %s\n",
	    st.skel.declared ? "yes, in /etc/synthetic.conf" : "no");
	printf("  present      %s\n", st.skel.active ? "yes, /mnt exists" : "no");

	if (st.skel.conflicting)
		printf("\n  note: /etc/synthetic.conf declares 'mnt' but not as ours%s%s.\n"
		       "        mSL will not modify it.\n",
		       st.skel.target[0] != '\0' ? " -> " : "",
		       st.skel.target[0] != '\0' ? st.skel.target : "");
	else if (st.reboot_pending)
		printf("\n  note: declared, but /mnt appears only after a reboot.\n");
	else if (st.skel.active)
		printf("\n  /mnt is empty, which is what it is on Linux. Nothing populates it.\n");

	return 0;
}

static int
mnt_command(const char *verb)
{
	if (verb == NULL || strcmp(verb, "status") == 0)
		return mnt_status();

	if (strcmp(verb, "enable") == 0)
		return msl_mnt_enable() == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return msl_mnt_disable() == 0 ? 0 : 1;

	msl_err("unknown command: mnt %s", verb);
	return 2;
}

/* One-word state for a node's Finder visibility. */
static const char *
vis_word(const struct msl_vis_status *st)
{
	if (!st->exists)
		return "absent";
	return st->hidden ? "hidden" : "shown";
}

static int
vis_list(void)
{
	printf("%-14s %-8s %-10s %s\n", "NODE", "FINDER", "FS", "NOTE");

	for (size_t i = 0; i < msl_root_node_count; i++) {
		const struct msl_node *node = &msl_root_nodes[i];
		struct msl_vis_status st;
		const char *reason;

		if (msl_vis_status(node->path, &st) != 0)
			continue;

		reason = msl_vis_lock_reason(st.lock);

		/*
		 * A node that exists but cannot be changed is the interesting case,
		 * so the reason is always shown rather than only on a failed attempt.
		 */
		printf("%-14s %-8s %-10s %s\n",
		    node->path,
		    vis_word(&st),
		    st.exists ? st.fstype : "-",
		    (st.exists && reason != NULL) ? reason
		        : (st.exists && st.is_mount && !st.browsable)
		            ? "mounted nobrowse: invisible to the Finder"
		            : "");
	}

	return 0;
}

static int
vis_command(int argc, char **argv)
{
	const char *verb = argc > 2 ? argv[2] : NULL;
	const struct msl_node *node;
	char reason[512];
	bool hide;

	if (verb == NULL || strcmp(verb, "list") == 0 || strcmp(verb, "status") == 0)
		return vis_list();

	if (strcmp(verb, "browse") == 0) {
		bool on;

		if (argc < 5 || (strcmp(argv[4], "on") != 0 && strcmp(argv[4], "off") != 0)) {
			msl_err("usage: mslctl vis browse <node> on|off");
			return 2;
		}

		node = msl_node_find(argv[3]);
		if (node == NULL) {
			msl_err("unknown node: %s", argv[3]);
			return 2;
		}

		on = strcmp(argv[4], "on") == 0;
		if (msl_vis_set_browsable(node->path, on, reason, sizeof(reason)) != 0) {
			msl_err("%s", reason);
			return 1;
		}

		msl_log("%s is now %s to the Finder", node->path,
		    on ? "browsable" : "hidden from browsing");
		return 0;
	}

	hide = strcmp(verb, "hide") == 0;
	if (!hide && strcmp(verb, "show") != 0) {
		msl_err("unknown command: vis %s", verb);
		return 2;
	}

	if (argc < 4) {
		msl_err("usage: mslctl vis %s <node>", verb);
		return 2;
	}

	node = msl_node_find(argv[3]);
	if (node == NULL) {
		msl_err("unknown node: %s", argv[3]);
		return 2;
	}

	if (msl_vis_set(node->path, hide, reason, sizeof(reason)) != 0) {
		msl_err("%s", reason);
		return 1;
	}

	msl_log("%s is now %s in the Finder", node->path, hide ? "hidden" : "shown");
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
	    "  mnt      /mnt, an empty directory (as on Linux)\n"
	    "  media    /media/<user>/<label> for removable volumes\n"
	    "\n"
	    "commands:\n"
	    "  status   show the current state (default)\n"
	    "  check    report whether the component is safe to enable\n"
	    "  enable   turn the component on           (root)\n"
	    "  disable  turn the component off          (root)\n"
	    "  sync     reconcile with the current state  (root)\n"
	    "\n"
	    "media also supports:\n"
	    "  list     show the removable volumes that would appear in /media\n"
	    "\n"
	    "  porcelain  machine-readable state, one key=value per line\n"
	    "\n"
	    "Finder visibility of the root-level directories:\n"
	    "  vis              list every node and whether it is hidden\n"
	    "  vis show <node>  clear the hidden flag         (root)\n"
	    "  vis hide <node>  set the hidden flag           (root)\n"
	    "  vis browse <node> on|off   clear or set the mount nobrowse flag (root)\n");
}

int
main(int argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		usage();
		return argc < 2 ? 2 : 0;
	}

	if (strcmp(argv[1], "porcelain") == 0)
		return porcelain();

	/* Bare `status` reports every component. */
	if (strcmp(argv[1], "status") == 0) {
		struct msl_pseudofs proc, sys;
		int rc = home_status();
		printf("\n");
		if (mnt_status() != 0)
			rc = 1;
		printf("\n");
		if (media_status() != 0)
			rc = 1;

		msl_detect_procfs(&proc);
		msl_detect_sysfs(&sys);
		printf("\npseudo-filesystems (managed by their own projects)\n");
		printf("  /proc        %s\n", describe(&proc));
		printf("  /sys         %s\n", describe(&sys));
		printf("\nmslxd        %s\n",
		    msl_daemon_running() ? "running" : "not running");
		return rc;
	}

	if (strcmp(argv[1], "home") == 0)
		return home_command(argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "mnt") == 0)
		return mnt_command(argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "media") == 0)
		return media_command(argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "vis") == 0)
		return vis_command(argc, argv);

	msl_err("unknown component: %s", argv[1]);
	usage();
	return 2;
}
