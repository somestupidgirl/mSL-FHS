/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhsctl.c
 *
 * Command-line control for the mSL/XNU layout layer. This is the mechanism the
 * daemon and GUI will drive; having it as a tool first means every component
 * can be exercised and verified before either of them exists.
 *
 *     fhsctl status              show every component
 *     fhsctl home status         show the /home component
 *     fhsctl home enable         requires root
 *     fhsctl home disable        requires root
 *     fhsctl home sync           requires root
 */
#include "fhs.h"
#include "fhs_boot.h"
#include "fhs_detect.h"
#include "fhs_home.h"
#include "fhs_media.h"
#include "fhs_mnt.h"
#include "fhs_simple.h"
#include "fhs_visibility.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* One-line summary of a pseudo-filesystem we only observe. */
static const char *
describe(const struct fhs_pseudofs *fs)
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
	struct fhs_home_status st;

	if (fhs_home_status(&st) != 0) {
		fhs_err("cannot read /home status");
		return 1;
	}

	printf("/home\n");
	printf("  state        %s\n", st.enabled ? "enabled" : "disabled");
	printf("  automounter  %s\n",
	    st.automounter ? "active (auto_home owns /home)" : "released");
	printf("  auto_master  %s\n", st.masked ? "masked by mSL" : "unmodified");
	printf("  synthetic    %s\n",
	    st.skel.declared ? "declared in /etc/synthetic.conf" : "not declared");
	printf("  present      %s\n", st.skel.active ? "yes, /home exists" : "no");
	printf("  accounts     %d eligible\n", st.users);
	printf("  links        %d\n", st.links);

	if (st.foreign > 0)
		printf("  foreign      %d entry(s) not created by mSL (left untouched)\n",
		    st.foreign);

	/*
	 * Report the states that are internally inconsistent, since they are what
	 * a user is most likely to need help with.
	 */
	if (st.enabled && st.reboot_pending)
		printf("\n  note: /home is declared but not present. macOS creates\n"
		       "        root-level entries only at startup, so it appears\n"
		       "        after you restart. The symlinks below it are ready.\n");
	else if (st.enabled && st.automounter)
		printf("\n  note: enabled, but the automounter still owns %s.\n"
		       "        Run 'sudo fhsctl home enable' again, or reboot.\n",
		       FHS_HOME_ROOT);
	else if (st.enabled && st.links < st.users)
		printf("\n  note: %d account(s) have no /home entry. "
		       "Run 'sudo fhsctl home sync'.\n", st.users - st.links);

	return 0;
}

/* Stable one-word name for a visibility lock, for the porcelain interface. */
static const char *
vis_lock_word(enum fhs_vis_lock lock)
{
	switch (lock) {
	case FHS_VIS_CHANGEABLE: return "changeable";
	case FHS_VIS_ABSENT:     return "absent";
	case FHS_VIS_SIP:        return "sip";
	case FHS_VIS_READONLY:   return "readonly";
	case FHS_VIS_UNSUPPORTED: return "unsupported";
	case FHS_VIS_PROTECTED:  return "protected";
	}
	return "unknown";
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
	struct fhs_home_status home;
	struct fhs_mnt_status mnt;
	struct fhs_media_status media;
	struct fhs_pseudofs proc, sys;

	if (fhs_home_status(&home) == 0) {
		printf("home.enabled=%d\n", home.enabled);
		printf("home.automounter=%d\n", home.automounter);
		printf("home.masked=%d\n", home.masked);
		printf("home.users=%d\n", home.users);
		printf("home.links=%d\n", home.links);
		printf("home.foreign=%d\n", home.foreign);
		printf("home.declared=%d\n", home.skel.declared);
		printf("home.active=%d\n", home.skel.active);
		printf("home.reboot_pending=%d\n", home.reboot_pending);
	}

	if (fhs_mnt_status(&mnt) == 0) {
		printf("mnt.enabled=%d\n", mnt.enabled);
		printf("mnt.declared=%d\n", mnt.skel.declared);
		printf("mnt.conflicting=%d\n", mnt.skel.conflicting);
		printf("mnt.active=%d\n", mnt.skel.active);
		printf("mnt.reboot_pending=%d\n", mnt.reboot_pending);
		printf("mnt.mounts=%d\n", mnt.mounts);

		/* The mounts themselves, so the GUI can list them. */
		struct fhs_mnt_mount mm[64];
		int nm = fhs_mnt_scan(mm, (int)(sizeof(mm) / sizeof(mm[0])));
		for (int i = 0; i < nm; i++) {
			printf("mnt.mount.%d.name=%s\n", i, mm[i].name);
			printf("mnt.mount.%d.path=%s\n", i, mm[i].path);
			printf("mnt.mount.%d.fstype=%s\n", i, mm[i].fstype);
		}
	}

	if (fhs_media_status(&media) == 0) {
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

	{
		struct fhs_boot_status boot;
		if (fhs_boot_status(&boot) == 0) {
			printf("boot.enabled=%d\n", boot.enabled);
			printf("boot.declared=%d\n", boot.skel.declared);
			printf("boot.conflicting=%d\n", boot.skel.conflicting);
			printf("boot.active=%d\n", boot.skel.active);
			printf("boot.reboot_pending=%d\n", boot.reboot_pending);
			printf("boot.links=%d\n", boot.links);
			printf("boot.foreign=%d\n", boot.foreign);
			printf("boot.running=%s\n", boot.running);
			printf("boot.kernel=%s\n", boot.kernel);
		}
	}

	for (size_t i = 0; i < fhs_simple_node_count; i++) {
		const struct fhs_simple_def *def = &fhs_simple_nodes[i];
		struct fhs_simple_status ss;

		if (fhs_simple_status(def, &ss) != 0)
			continue;

		printf("%s.enabled=%d\n", def->name, ss.enabled);
		printf("%s.declared=%d\n", def->name, ss.skel.declared);
		printf("%s.conflicting=%d\n", def->name, ss.skel.conflicting);
		printf("%s.active=%d\n", def->name, ss.skel.active);
		printf("%s.reboot_pending=%d\n", def->name, ss.reboot_pending);
		printf("%s.target_missing=%d\n", def->name, ss.target_missing);
	}

	{
		struct fhs_pseudofs dev;
		fhs_detect_devfs(&dev);
		printf("dev.installed=%d\n", dev.installed);
		printf("dev.mounted=%d\n", dev.mounted);
	}

	fhs_detect_procfs(&proc);
	fhs_detect_sysfs(&sys);
	printf("proc.installed=%d\n", proc.installed);
	printf("proc.mounted=%d\n", proc.mounted);
	printf("sys.installed=%d\n", sys.installed);
	printf("sys.mounted=%d\n", sys.mounted);

	printf("daemon.running=%d\n", fhs_daemon_running());
	printf("version=%s\n", FHS_VERSION);

	/*
	 * Every root-level node, so the menu can present them all with a
	 * per-node dropdown. The order is the table's, which is what the GUI
	 * displays; `nodes` gives it that order without hard-coding the list.
	 */
	printf("nodes=");
	for (size_t i = 0; i < fhs_root_node_count; i++)
		printf("%s%s", i ? "," : "", fhs_root_nodes[i].path + 1);
	printf("\n");

	for (size_t i = 0; i < fhs_root_node_count; i++) {
		const struct fhs_node *node = &fhs_root_nodes[i];
		const char *name = node->path + 1;   /* drop the leading slash */
		struct fhs_vis_status vs;

		if (fhs_vis_status(node->path, &vs) != 0)
			continue;

		printf("vis.%s.exists=%d\n",    name, vs.exists);
		printf("vis.%s.hidden=%d\n",    name, vs.hidden);
		printf("vis.%s.lock=%s\n",      name, vis_lock_word(vs.lock));
		printf("vis.%s.linux=%d\n",     name, node->linux_only);
		printf("vis.%s.mount=%d\n",     name, vs.is_mount);
		printf("vis.%s.browsable=%d\n", name, vs.browsable);
		printf("vis.%s.fstype=%s\n",    name, vs.exists ? vs.fstype : "");
	}

	return 0;
}

/* One of the skeleton-only nodes: /root, /run, /srv. */
static int
simple_status(const struct fhs_simple_def *def)
{
	struct fhs_simple_status st;

	if (fhs_simple_status(def, &st) != 0) {
		fhs_err("cannot read /%s status", def->name);
		return 1;
	}

	/*
	 * A missing target is only a fault when we were relying on it being
	 * there: for /srv, which we create ourselves, "missing while disabled" is
	 * simply the state before it is turned on.
	 */
	bool target_fault = st.target_missing &&
	    (!def->creates_target || st.enabled);

	printf("/%s\n", def->name);
	printf("  state        %s\n", st.enabled ? "enabled" : "disabled");
	printf("  target       %s%s\n", def->target,
	    target_fault ? " (missing)" : "");
	printf("  declared     %s\n",
	    st.skel.declared ? "yes, in /etc/synthetic.conf" : "no");
	printf("  present      %s\n", st.skel.active ? "yes" : "no");

	if (st.skel.conflicting)
		printf("\n  note: /etc/synthetic.conf declares '%s' but not as ours.\n"
		       "        mSL will not modify it.\n", def->name);
	else if (target_fault)
		printf("\n  note: the target %s does not exist, so /%s would dangle.\n",
		       def->target, def->name);
	else if (st.reboot_pending)
		printf("\n  note: declared, but /%s appears only after a reboot.\n",
		       def->name);

	return 0;
}

static int
simple_command(const struct fhs_simple_def *def, const char *verb)
{
	if (verb == NULL || strcmp(verb, "status") == 0)
		return simple_status(def);

	if (strcmp(verb, "enable") == 0)
		return fhs_simple_enable(def) == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return fhs_simple_disable(def) == 0 ? 0 : 1;

	fhs_err("unknown command: %s %s", def->name, verb);
	return 2;
}

static int
boot_status(void)
{
	struct fhs_boot_status st;

	if (fhs_boot_status(&st) != 0) {
		fhs_err("cannot read /boot status");
		return 1;
	}

	printf("/boot\n");
	printf("  state        %s\n", st.enabled ? "enabled" : "disabled");
	printf("  declared     %s\n",
	    st.skel.declared ? "yes, in /etc/synthetic.conf" : "no");
	printf("  present      %s\n", st.skel.active ? "yes, /boot exists" : "no");
	printf("  kernel       %s\n", st.running);
	printf("               %s\n", st.kernel);
	printf("  links        %d\n", st.links);

	if (st.foreign > 0)
		printf("  other        %d entry(s) not created by mSL (left untouched)\n",
		    st.foreign);

	if (st.skel.conflicting)
		printf("\n  note: /etc/synthetic.conf declares 'boot' but not as ours.\n"
		       "        mSL will not modify it.\n");
	else if (st.reboot_pending)
		printf("\n  note: declared, but /boot appears only after a reboot.\n"
		       "        The symlinks below it are already in place.\n");

	return 0;
}

static int
boot_command(const char *verb)
{
	if (verb == NULL || strcmp(verb, "status") == 0)
		return boot_status();

	if (strcmp(verb, "enable") == 0)
		return fhs_boot_enable() == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return fhs_boot_disable() == 0 ? 0 : 1;

	if (strcmp(verb, "sync") == 0)
		return fhs_boot_sync() == 0 ? 0 : 1;

	fhs_err("unknown command: boot %s", verb);
	return 2;
}

static int
media_status(void)
{
	struct fhs_media_status st;

	if (fhs_media_status(&st) != 0) {
		fhs_err("cannot read /media status");
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
		       "        Run 'sudo fhsctl media sync' to clear them. Until fhsxd\n"
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
		return fhs_media_enable() == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return fhs_media_disable() == 0 ? 0 : 1;

	if (strcmp(verb, "sync") == 0)
		return fhs_media_sync() == 0 ? 0 : 1;

	if (strcmp(verb, "list") == 0) {
		struct fhs_volume vols[64];
		int n = fhs_media_scan(vols, 64);

		if (n < 0) {
			fhs_err("cannot enumerate volumes");
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

	fhs_err("unknown command: media %s", verb);
	return 2;
}

static int
mnt_status(void)
{
	struct fhs_mnt_status st;

	if (fhs_mnt_status(&st) != 0) {
		fhs_err("cannot read /mnt status");
		return 1;
	}

	printf("/mnt\n");
	printf("  state        %s\n", st.enabled ? "enabled" : "disabled");
	printf("  declared     %s\n",
	    st.skel.declared ? "yes, in /etc/synthetic.conf" : "no");
	printf("  present      %s\n", st.skel.active ? "yes, /mnt exists" : "no");

	if (st.mounts > 0) {
		struct fhs_mnt_mount mm[64];
		int n = fhs_mnt_scan(mm, (int)(sizeof(mm) / sizeof(mm[0])));

		printf("  mounts       %d\n", st.mounts);
		for (int i = 0; i < n; i++)
			printf("    %s mounted at %s %s\n",
			    mm[i].name, mm[i].path, mm[i].fstype);
	} else {
		printf("  mounts       none\n");
	}

	if (st.skel.conflicting)
		printf("\n  note: /etc/synthetic.conf declares 'mnt' but not as ours%s%s.\n"
		       "        mSL will not modify it.\n",
		       st.skel.target[0] != '\0' ? " -> " : "",
		       st.skel.target[0] != '\0' ? st.skel.target : "");
	else if (st.reboot_pending)
		printf("\n  note: declared, but /mnt appears only after a reboot.\n");
	else if (st.skel.active && st.mounts == 0)
		printf("\n  /mnt exists and is empty - ready for manual mounts,\n"
		       "  as on Linux (mkdir /mnt/disk && mount ... /mnt/disk).\n");

	return 0;
}

static int
mnt_command(const char *verb)
{
	if (verb == NULL || strcmp(verb, "status") == 0)
		return mnt_status();

	if (strcmp(verb, "enable") == 0)
		return fhs_mnt_enable() == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return fhs_mnt_disable() == 0 ? 0 : 1;

	fhs_err("unknown command: mnt %s", verb);
	return 2;
}

/* One-word state for a node's Finder visibility. */
static const char *
vis_word(const struct fhs_vis_status *st)
{
	if (!st->exists)
		return "absent";
	return st->hidden ? "hidden" : "shown";
}

static int
vis_list(void)
{
	printf("%-14s %-8s %-10s %s\n", "NODE", "FINDER", "FS", "NOTE");

	for (size_t i = 0; i < fhs_root_node_count; i++) {
		const struct fhs_node *node = &fhs_root_nodes[i];
		struct fhs_vis_status st;
		const char *reason;

		if (fhs_vis_status(node->path, &st) != 0)
			continue;

		reason = fhs_vis_lock_reason(st.lock);

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
	const struct fhs_node *node;
	char reason[512];
	bool hide;

	if (verb == NULL || strcmp(verb, "list") == 0 || strcmp(verb, "status") == 0)
		return vis_list();

	if (strcmp(verb, "browse") == 0) {
		bool on;

		if (argc < 5 || (strcmp(argv[4], "on") != 0 && strcmp(argv[4], "off") != 0)) {
			fhs_err("usage: fhsctl vis browse <node> on|off");
			return 2;
		}

		node = fhs_node_find(argv[3]);
		if (node == NULL) {
			fhs_err("unknown node: %s", argv[3]);
			return 2;
		}

		on = strcmp(argv[4], "on") == 0;
		if (fhs_vis_set_browsable(node->path, on, reason, sizeof(reason)) != 0) {
			fhs_err("%s", reason);
			return 1;
		}

		fhs_log("%s is now %s to the Finder", node->path,
		    on ? "browsable" : "hidden from browsing");
		return 0;
	}

	hide = strcmp(verb, "hide") == 0;
	if (!hide && strcmp(verb, "show") != 0) {
		fhs_err("unknown command: vis %s", verb);
		return 2;
	}

	if (argc < 4) {
		fhs_err("usage: fhsctl vis %s <node>", verb);
		return 2;
	}

	node = fhs_node_find(argv[3]);
	if (node == NULL) {
		fhs_err("unknown node: %s", argv[3]);
		return 2;
	}

	if (fhs_vis_set(node->path, hide, reason, sizeof(reason)) != 0) {
		fhs_err("%s", reason);
		return 1;
	}

	fhs_log("%s is now %s in the Finder", node->path, hide ? "hidden" : "shown");
	return 0;
}

static int
home_command(const char *verb)
{
	if (verb == NULL || strcmp(verb, "status") == 0)
		return home_status();

	if (strcmp(verb, "enable") == 0)
		return fhs_home_enable() == 0 ? 0 : 1;

	if (strcmp(verb, "disable") == 0)
		return fhs_home_disable() == 0 ? 0 : 1;

	if (strcmp(verb, "sync") == 0)
		return fhs_home_sync() == 0 ? 0 : 1;

	if (strcmp(verb, "check") == 0) {
		char reason[512];
		if (fhs_home_check_safe(reason, sizeof(reason)) != 0) {
			printf("unsafe: %s\n", reason);
			return 1;
		}
		printf("safe: no automounter home directories detected\n");
		return 0;
	}

	fhs_err("unknown command: home %s", verb);
	return 2;
}

/*
 * Complete teardown: switch every component off, then remove everything this
 * project installed.
 *
 * This lives here, rather than being repeated in the uninstaller app, the
 * disk image's shell script and `make uninstall`, because it is the one place
 * that knows the full component list. The earlier copies each disabled only
 * home, mnt and media - so boot, root, run and srv kept their
 * /etc/synthetic.conf entries after an "uninstall", leaving root-level
 * symlinks behind with the tool that made them gone.
 *
 * Order matters. Disabling comes first and needs both this binary and the
 * persisted state present: it is what restores /etc/auto_master and withdraws
 * the synthetic.conf entries. Removing the files first would strand the
 * system with a masked automounter line and no supported way to put it back.
 */
static int
uninstall_all(void)
{
	static const char *const files[] = {
		"/Library/LaunchDaemons/com.beako.fhsxd.plist",
		"/usr/local/sbin/fhsxd",
		"/var/db/fhs.home", "/var/db/fhs.mnt", "/var/db/fhs.media",
		"/var/db/fhs.boot", "/var/db/fhs.root", "/var/db/fhs.run",
		"/var/db/fhs.srv",
		NULL
	};
	static const char *const bundles[] = {
		"/Applications/mSL/FHS.app",
		"/Applications/mSL/Uninstall-FHS.app",
		"/Library/PreferencePanes/FHS.prefPane",
		NULL
	};

	if (!fhs_is_root()) {
		fhs_err("uninstalling requires root");
		return 1;
	}

	fhs_log("Disabling components");
	fhs_media_disable();
	fhs_boot_disable();
	fhs_mnt_disable();
	for (size_t i = 0; i < fhs_simple_node_count; i++)
		fhs_simple_disable(&fhs_simple_nodes[i]);
	/* Last, so a failure above still leaves auto_master in a known state. */
	fhs_home_disable();

	fhs_log("Stopping the daemon");
	{
		const char *const argv[] = { "/bin/launchctl", "bootout",
		                             "system/com.beako.fhsxd", NULL };
		fhs_run(argv);
	}

	fhs_log("Removing files");
	for (int i = 0; files[i] != NULL; i++) {
		if (unlink(files[i]) == 0)
			fhs_log("  removed %s", files[i]);
	}
	for (int i = 0; bundles[i] != NULL; i++) {
		const char *const argv[] = { "/bin/rm", "-rf", bundles[i], NULL };
		if (access(bundles[i], F_OK) == 0 && fhs_run(argv) == 0)
			fhs_log("  removed %s", bundles[i]);
	}

	/* Only if this was the last module in it. */
	rmdir("/Applications/mSL");

	{
		const char *const argv[] = { "/usr/sbin/pkgutil", "--forget",
		                             "com.beako.fhs.pkg", NULL };
		fhs_run(argv);
	}

	fhs_log("");
	fhs_log("mSL/FHS has been removed.");
	fhs_log("Your original /etc/auto_master is kept at "
	        "/var/db/fhs.auto_master.orig.");
	fhs_log("Root-level directories remain until the next restart.");

	/*
	 * This binary goes last, and removes itself. Unlinking a running
	 * executable is fine on a UNIX system: the file is gone from the
	 * directory, and this process keeps running from the open inode.
	 */
	unlink("/usr/local/sbin/fhsctl");
	return 0;
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: fhsctl <component> <command>\n"
	    "\n"
	    "components:\n"
	    "  home     Linux-style /home/<user> paths for local accounts\n"
	    "  mnt      /mnt, an empty directory (as on Linux)\n"
	    "  media    /media/<user>/<label> for removable volumes\n"
	    "  root     /root -> /var/root, the superuser's home\n"
	    "  run      /run -> /var/run, runtime state\n"
	    "  srv      /srv, empty (as on Linux)\n"
	    "  boot     /boot, the kernel and bootloader artifacts\n"
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
	    "  vis browse <node> on|off   clear or set the mount nobrowse flag (root)\n"
	    "\n"
	    "  uninstall  switch every component off and remove mSL/FHS   (root)\n");
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
		struct fhs_pseudofs proc, sys;
		int rc = home_status();
		printf("\n");
		if (mnt_status() != 0)
			rc = 1;
		printf("\n");
		if (media_status() != 0)
			rc = 1;

		printf("\n");
		if (boot_status() != 0)
			rc = 1;

		for (size_t i = 0; i < fhs_simple_node_count; i++) {
			printf("\n");
			if (simple_status(&fhs_simple_nodes[i]) != 0)
				rc = 1;
		}

		fhs_detect_procfs(&proc);
		fhs_detect_sysfs(&sys);
		struct fhs_pseudofs dev;
		fhs_detect_devfs(&dev);
		printf("\npseudo-filesystems (managed by their own projects)\n");
		printf("  /dev         %s\n", describe(&dev));
		printf("  /proc        %s\n", describe(&proc));
		printf("  /sys         %s\n", describe(&sys));
		printf("\nfhsxd        %s\n",
		    fhs_daemon_running() ? "running" : "not running");
		return rc;
	}

	if (strcmp(argv[1], "home") == 0)
		return home_command(argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "mnt") == 0)
		return mnt_command(argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "media") == 0)
		return media_command(argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "uninstall") == 0)
		return uninstall_all();

	if (strcmp(argv[1], "boot") == 0)
		return boot_command(argc > 2 ? argv[2] : NULL);

	if (strcmp(argv[1], "vis") == 0)
		return vis_command(argc, argv);

	{
		const struct fhs_simple_def *def = fhs_simple_find(argv[1]);
		if (def != NULL)
			return simple_command(def, argc > 2 ? argv[2] : NULL);
	}

	fhs_err("unknown component: %s", argv[1]);
	usage();
	return 2;
}
