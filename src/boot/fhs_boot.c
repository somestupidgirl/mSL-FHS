/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_boot.c
 *
 * The /boot component. See fhs_boot.h for the layout and the two decisions
 * behind it.
 */
#include "fhs.h"
#include "fhs_boot.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

/* Where macOS keeps the artifacts. All on the sealed system volume. */
#define KERNELS_DIR     "/System/Library/Kernels"
#define COLLECTIONS_DIR "/System/Library/KernelCollections"
#define PRELINKED_DIR   "/System/Library/PrelinkedKernels"
#define BOOT_EFI        "/System/Library/CoreServices/boot.efi"

/* Only symlinks pointing here are ours to remove. */
#define SYSTEM_LIBRARY  "/System/Library/"

/* The subdirectories we create and maintain inside /boot. */
static const char *const boot_subdirs[] = {
	FHS_BOOT_KERNELS, FHS_BOOT_EFI, FHS_BOOT_COLLECTIONS
};
#define BOOT_SUBDIR_COUNT \
	(sizeof(boot_subdirs) / sizeof(boot_subdirs[0]))

/* A symlink the component maintains. */
struct link {
	char path[MAXPATHLEN];      /* absolute, under FHS_BOOT_ROOT */
	char target[MAXPATHLEN];
};

#define MAX_LINKS 64

/* ---------------------------------------------------------------------------
 * Which kernel this machine boots
 * ------------------------------------------------------------------------- */

bool
fhs_boot_running_kernel(char *path, size_t path_len, char *name, size_t name_len)
{
	char version[512], release[64], soc[32];
	size_t len;
	const char *tag;
	struct stat sb;

	/* The Darwin release, e.g. "25.5.0" - the same string uname -r prints. */
	len = sizeof(release);
	if (sysctlbyname("kern.osrelease", release, &len, NULL, 0) != 0)
		snprintf(release, sizeof(release), "unknown");

	snprintf(name, name_len, "darwin-%s", release);

	/*
	 * kern.version names the build configuration, e.g.
	 *   "... root:xnu-12377.121.10~1/RELEASE_ARM64_T8142"
	 * whose SoC tag selects the kernel image among the dozen or so shipped.
	 * This is the same derivation procfs uses to find the running kernel's
	 * Mach-O for its symbol table.
	 */
	len = sizeof(version);
	if (sysctlbyname("kern.version", version, &len, NULL, 0) == 0 &&
	    (tag = strstr(version, "RELEASE_ARM64_")) != NULL) {
		tag += strlen("RELEASE_ARM64_");

		size_t i = 0;
		while (i < sizeof(soc) - 1 && tag[i] != '\0' &&
		       (isalnum((unsigned char)tag[i]) != 0)) {
			soc[i] = (char)tolower((unsigned char)tag[i]);
			i++;
		}
		soc[i] = '\0';

		if (i > 0) {
			snprintf(path, path_len, "%s/kernel.release.%s",
			    KERNELS_DIR, soc);
			if (stat(path, &sb) == 0)
				return true;
		}
	}

	/*
	 * Intel machines, virtual machines, and anything whose per-SoC image is
	 * absent fall back to the generic kernel. It is the right answer there
	 * rather than a degraded one.
	 */
	snprintf(path, path_len, "%s/kernel", KERNELS_DIR);
	return stat(path, &sb) == 0;
}

/* ---------------------------------------------------------------------------
 * Building the desired set
 * ------------------------------------------------------------------------- */

static void
add_link(struct link *out, int *n, const char *subdir, const char *name,
    const char *target)
{
	struct stat sb;

	if (*n >= MAX_LINKS)
		return;

	/* Never link something that is not there. */
	if (stat(target, &sb) != 0)
		return;

	if (subdir != NULL && subdir[0] != '\0')
		snprintf(out[*n].path, sizeof(out[*n].path), "%s/%s/%s",
		    FHS_BOOT_ROOT, subdir, name);
	else
		snprintf(out[*n].path, sizeof(out[*n].path), "%s/%s",
		    FHS_BOOT_ROOT, name);

	snprintf(out[*n].target, sizeof(out[*n].target), "%s", target);
	(*n)++;
}

/* Link every regular file in `dir` into `subdir`, optionally by suffix. */
static void
add_dir(struct link *out, int *n, const char *subdir, const char *dir,
    const char *suffix)
{
	DIR *d;
	struct dirent *ent;
	char target[MAXPATHLEN];

	d = opendir(dir);
	if (d == NULL)
		return;

	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		if (suffix != NULL) {
			size_t nlen = strlen(ent->d_name), slen = strlen(suffix);
			if (nlen < slen || strcmp(ent->d_name + nlen - slen, suffix) != 0)
				continue;
		}

		snprintf(target, sizeof(target), "%s/%s", dir, ent->d_name);
		add_link(out, n, subdir, ent->d_name, target);
	}

	closedir(d);
}

/*
 * The full set of symlinks /boot should contain.
 *
 * Top level holds the kernel this machine boots, under a version-qualified
 * name and a stable alias - the shape Linux uses for vmlinuz. Everything else
 * is grouped into subdirectories so the kernels stay prominent.
 */
static int
desired(struct link *out, int max)
{
	char kernel[MAXPATHLEN], name[64];
	int n = 0;

	(void)max;

	if (fhs_boot_running_kernel(kernel, sizeof(kernel), name, sizeof(name))) {
		add_link(out, &n, NULL, name, kernel);
		add_link(out, &n, NULL, "darwin", kernel);
	}

	add_dir(out, &n, FHS_BOOT_KERNELS, KERNELS_DIR, NULL);
	add_link(out, &n, FHS_BOOT_EFI, "boot.efi", BOOT_EFI);
	add_dir(out, &n, FHS_BOOT_COLLECTIONS, COLLECTIONS_DIR, ".kc");

	/*
	 * PrelinkedKernels is the Intel-era mechanism and is empty on Apple
	 * silicon, where KernelCollections replaced it. Linked when present so an
	 * Intel machine gets its prelinked image too; add_dir simply finds nothing
	 * otherwise.
	 */
	add_dir(out, &n, FHS_BOOT_COLLECTIONS, PRELINKED_DIR, NULL);

	return n;
}

/* ---------------------------------------------------------------------------
 * The farm
 * ------------------------------------------------------------------------- */

/*
 * Is `path` a symlink of ours? Only links into /System/Library are, which is
 * what keeps a Linux kernel image - or anything else placed in /boot by hand -
 * safe from pruning.
 */
static bool
ours(const char *path, char *target, size_t target_len)
{
	struct stat sb;
	ssize_t n;

	if (lstat(path, &sb) != 0 || !S_ISLNK(sb.st_mode))
		return false;

	n = readlink(path, target, target_len - 1);
	if (n < 0)
		return false;
	target[n] = '\0';

	return strncmp(target, SYSTEM_LIBRARY, sizeof(SYSTEM_LIBRARY) - 1) == 0;
}

/* Remove links of ours under `dir` that are not in `keep`. */
static int
prune(const char *dir, const struct link *keep, int nkeep)
{
	DIR *d;
	struct dirent *ent;
	char path[MAXPATHLEN], target[MAXPATHLEN];
	int removed = 0;

	d = opendir(dir);
	if (d == NULL)
		return 0;

	while ((ent = readdir(d)) != NULL) {
		bool wanted = false;

		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

		if (!ours(path, target, sizeof(target)))
			continue;

		for (int i = 0; i < nkeep; i++) {
			if (strcmp(keep[i].path, path) == 0 &&
			    strcmp(keep[i].target, target) == 0) {
				wanted = true;
				break;
			}
		}

		if (wanted)
			continue;

		if (unlink(path) != 0) {
			fhs_err("cannot remove %s: %s", path, strerror(errno));
			continue;
		}

		fhs_log("  unlinked %s", path);
		removed++;
	}

	closedir(d);
	return removed;
}

static int
ensure_dir(const char *path)
{
	if (mkdir(path, 0755) != 0 && errno != EEXIST) {
		fhs_err("cannot create %s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

int
fhs_boot_sync(void)
{
	struct link want[MAX_LINKS];
	char dir[MAXPATHLEN], target[MAXPATHLEN];
	int n;

	if (!fhs_is_root()) {
		fhs_err("syncing /boot requires root");
		return -1;
	}

	if (fhs_state_get(FHS_BOOT_STATE, 0) == 0)
		return 0;	/* component is off */

	if (ensure_dir(FHS_BOOT_ROOT) != 0)
		return -1;

	for (size_t i = 0; i < BOOT_SUBDIR_COUNT; i++) {
		snprintf(dir, sizeof(dir), "%s/%s", FHS_BOOT_ROOT, boot_subdirs[i]);
		if (ensure_dir(dir) != 0)
			return -1;
	}

	n = desired(want, MAX_LINKS);

	for (int i = 0; i < n; i++) {
		struct stat sb;

		if (lstat(want[i].path, &sb) == 0) {
			if (ours(want[i].path, target, sizeof(target)) &&
			    strcmp(target, want[i].target) == 0)
				continue;	/* already correct */

			if (!ours(want[i].path, target, sizeof(target))) {
				fhs_err("skipping %s: not a symlink of ours", want[i].path);
				continue;
			}

			if (unlink(want[i].path) != 0) {
				fhs_err("cannot replace %s: %s", want[i].path,
				    strerror(errno));
				continue;
			}
		}

		if (symlink(want[i].target, want[i].path) != 0) {
			fhs_err("cannot create %s -> %s: %s", want[i].path,
			    want[i].target, strerror(errno));
			continue;
		}

		fhs_log("  linked %s -> %s", want[i].path, want[i].target);
	}

	/* Drop links to artifacts that have gone - after an OS update, say. */
	prune(FHS_BOOT_ROOT, want, n);
	for (size_t i = 0; i < BOOT_SUBDIR_COUNT; i++) {
		snprintf(dir, sizeof(dir), "%s/%s", FHS_BOOT_ROOT, boot_subdirs[i]);
		prune(dir, want, n);
	}

	return 0;
}

/* ---------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

static void
count_entries(const char *dir, int *links, int *foreign)
{
	DIR *d;
	struct dirent *ent;
	char path[MAXPATHLEN], target[MAXPATHLEN];
	struct stat sb;

	d = opendir(dir);
	if (d == NULL)
		return;

	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

		if (ours(path, target, sizeof(target))) {
			(*links)++;
		} else if (lstat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
			/* Our own subdirectories are structure, not contents. */
			if (strcmp(ent->d_name, FHS_BOOT_KERNELS) != 0 &&
			    strcmp(ent->d_name, FHS_BOOT_EFI) != 0 &&
			    strcmp(ent->d_name, FHS_BOOT_COLLECTIONS) != 0)
				(*foreign)++;
		} else {
			(*foreign)++;
		}
	}

	closedir(d);
}

int
fhs_boot_status(struct fhs_boot_status *st)
{
	char dir[MAXPATHLEN];

	memset(st, 0, sizeof(*st));

	st->enabled = fhs_state_get(FHS_BOOT_STATE, 0) != 0;

	if (fhs_skeleton_status(FHS_BOOT_NAME, &st->skel) != 0)
		return -1;

	st->reboot_pending = fhs_skeleton_reboot_pending(FHS_BOOT_NAME);

	fhs_boot_running_kernel(st->kernel, sizeof(st->kernel),
	    st->running, sizeof(st->running));

	count_entries(FHS_BOOT_ROOT, &st->links, &st->foreign);
	for (size_t i = 0; i < BOOT_SUBDIR_COUNT; i++) {
		snprintf(dir, sizeof(dir), "%s/%s", FHS_BOOT_ROOT, boot_subdirs[i]);
		count_entries(dir, &st->links, &st->foreign);
	}

	return 0;
}

int
fhs_boot_enable(void)
{
	int rc;

	if (!fhs_is_root()) {
		fhs_err("enabling /boot requires root");
		return -1;
	}

	rc = fhs_skeleton_add(FHS_BOOT_NAME);
	if (rc < 0)
		return -1;

	if (fhs_state_set(FHS_BOOT_STATE, 1) != 0)
		fhs_err("warning: could not persist state: %s", strerror(errno));

	fhs_log("populating %s", FHS_BOOT_ROOT);
	if (fhs_boot_sync() != 0)
		return -1;

	if (fhs_skeleton_reboot_pending(FHS_BOOT_NAME))
		fhs_log("/boot will appear after the next reboot.");

	return 0;
}

int
fhs_boot_disable(void)
{
	char dir[MAXPATHLEN];
	int rc;

	if (!fhs_is_root()) {
		fhs_err("disabling /boot requires root");
		return -1;
	}

	fhs_log("removing symlinks from %s", FHS_BOOT_ROOT);
	for (size_t i = 0; i < BOOT_SUBDIR_COUNT; i++) {
		snprintf(dir, sizeof(dir), "%s/%s", FHS_BOOT_ROOT, boot_subdirs[i]);
		prune(dir, NULL, 0);
	}
	prune(FHS_BOOT_ROOT, NULL, 0);

	rc = fhs_skeleton_remove(FHS_BOOT_NAME);
	if (rc < 0)
		return -1;

	if (fhs_state_set(FHS_BOOT_STATE, 0) != 0)
		fhs_err("warning: could not persist state: %s", strerror(errno));

	if (rc == 1)
		fhs_log("/boot will disappear after the next reboot.");

	return 0;
}
