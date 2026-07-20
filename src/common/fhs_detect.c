/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_detect.c
 *
 * Read-only detection of the pseudo-filesystems and of our own daemon. Nothing
 * here mounts, unmounts, loads or configures anything - see fhs_detect.h.
 */
#include "fhs.h"
#include "fhs_detect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <unistd.h>

/*
 * Where a pseudo-filesystem's payload lands. macOS filesystem bundles go in
 * /Library/Filesystems; the kext that backs them goes in /Library/Extensions.
 * Either being present means the software is installed, even if the current
 * boot has not loaded it.
 */
static bool
installed(const char *fsname)
{
	char path[256];

	snprintf(path, sizeof(path), "/Library/Filesystems/%s.fs", fsname);
	if (access(path, F_OK) == 0)
		return true;

	snprintf(path, sizeof(path), "/Library/Extensions/%s.kext", fsname);
	return access(path, F_OK) == 0;
}

/* Is `fsname` mounted at `mountpoint` right now? */
static bool
mounted_at(const char *fsname, const char *mountpoint)
{
	struct statfs *mounts;
	int n;

	n = getmntinfo(&mounts, MNT_NOWAIT);
	if (n <= 0)
		return false;

	for (int i = 0; i < n; i++) {
		if (strcmp(mounts[i].f_mntonname, mountpoint) == 0 &&
		    strcmp(mounts[i].f_fstypename, fsname) == 0)
			return true;
	}

	return false;
}

static void
detect(struct fhs_pseudofs *fs, const char *name, const char *mountpoint)
{
	fs->name = name;
	fs->mountpoint = mountpoint;
	fs->installed = installed(name);
	fs->mounted = mounted_at(name, mountpoint);

	/*
	 * A mounted filesystem is installed by definition, whatever the on-disk
	 * layout suggests - it could have been loaded from somewhere else.
	 */
	if (fs->mounted)
		fs->installed = true;
}

void
fhs_detect_procfs(struct fhs_pseudofs *fs)
{
	detect(fs, "procfs", "/proc");
}

void
fhs_detect_sysfs(struct fhs_pseudofs *fs)
{
	detect(fs, "sysfs", "/sys");
}

void
fhs_detect_devfs(struct fhs_pseudofs *fs)
{
	fs->name = "devfs";
	fs->mountpoint = "/dev";

	/*
	 * Part of the kernel, so there is nothing to install and nothing to look
	 * for on disk - the generic detect() would report it as absent because no
	 * devfs.fs bundle exists.
	 */
	fs->installed = true;
	fs->mounted = mounted_at("devfs", "/dev");
}

bool
fhs_daemon_running(void)
{
	/*
	 * Ask the kernel for the process list rather than shelling out to pgrep:
	 * this is called from a status path that may run often, and spawning a
	 * process to answer "is a process running" is disproportionate.
	 */
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	struct kinfo_proc *procs;
	size_t len = 0;
	bool found = false;

	if (sysctl(mib, 4, NULL, &len, NULL, 0) != 0 || len == 0)
		return false;

	procs = malloc(len);
	if (procs == NULL)
		return false;

	if (sysctl(mib, 4, procs, &len, NULL, 0) != 0) {
		free(procs);
		return false;
	}

	for (size_t i = 0; i < len / sizeof(struct kinfo_proc); i++) {
		if (strcmp(procs[i].kp_proc.p_comm, "fhsxd") == 0) {
			found = true;
			break;
		}
	}

	free(procs);
	return found;
}
