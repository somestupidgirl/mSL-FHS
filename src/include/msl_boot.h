/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * msl_boot.h
 *
 * The /boot component: the boot artifacts this machine actually has, under the
 * names and shape Linux uses.
 *
 * On Linux /boot holds the kernel (`vmlinuz-<version>`), an initial ramdisk,
 * and bootloader files. macOS has real counterparts for each - they are simply
 * scattered under /System/Library and named differently:
 *
 *   /System/Library/Kernels/kernel.release.<soc>   the kernel this machine boots
 *   /System/Library/Kernels/kernel                 the generic kernel
 *   /System/Library/CoreServices/boot.efi          the bootloader
 *   /System/Library/KernelCollections (.kc files)  kexts linked into bootable
 *                                                  collections - the nearest
 *                                                  thing to a prelinked image
 *
 * so /boot gathers them by symlink. Nothing is copied and nothing is modified:
 * the artifacts stay where macOS put them, on the sealed system volume.
 *
 * Two decisions worth stating.
 *
 * The kernel is named `darwin-<release>`, mirroring Linux's `vmlinuz-<version>`
 * convention without adopting the name. A `vmlinuz` symlink would be a lie of
 * exactly the kind this project avoids elsewhere: a program opening it expecting
 * an ELF Linux kernel would get a Mach-O.
 *
 * And /boot is a directory the layer owns on the writable Data volume, not a
 * symlink onto a read-only system path. That costs nothing now and leaves the
 * directory writable, so a real Linux kernel image can be placed alongside the
 * Darwin ones later without rearranging anything. Pruning only ever removes
 * symlinks pointing into /System/Library, so anything else put there is left
 * alone.
 */
#ifndef MSL_BOOT_H
#define MSL_BOOT_H

#include <stdbool.h>
#include <sys/param.h>

#include "msl_skeleton.h"

#define MSL_BOOT_STATE  "msl.boot"
#define MSL_BOOT_NAME   "boot"
#define MSL_BOOT_ROOT   MSL_DATA_ROOT "/boot"

/* Subdirectories, kept out of the top level so the kernels stay prominent. */
#define MSL_BOOT_KERNELS      "kernels"
#define MSL_BOOT_EFI          "efi"
#define MSL_BOOT_COLLECTIONS  "kernelcollections"

struct msl_boot_status {
	bool enabled;
	bool reboot_pending;
	struct msl_skeleton_status skel;
	int  links;                 /* symlinks we maintain */
	int  foreign;               /* entries that are not ours, left untouched */
	char running[64];           /* e.g. "darwin-25.5.0" */
	char kernel[MAXPATHLEN];    /* the kernel this machine boots */
};

int msl_boot_status(struct msl_boot_status *st);

/* Declare the skeleton entry and populate. Requires root. */
int msl_boot_enable(void);

/* Withdraw the entry and remove only our symlinks. Requires root. */
int msl_boot_disable(void);

/* Reconcile the farm with the artifacts present now. Requires root. */
int msl_boot_sync(void);

/*
 * The kernel image this machine boots, and the name it is given in /boot.
 * `name` receives "darwin-<release>". Returns false if it cannot be determined,
 * in which case the generic kernel is used.
 */
bool msl_boot_running_kernel(char *path, size_t path_len,
                             char *name, size_t name_len);

#endif /* MSL_BOOT_H */
