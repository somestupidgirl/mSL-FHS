/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * fhs_simple.c
 *
 * The skeleton-only nodes: /root, /run and /srv. See fhs_simple.h.
 */
#include "fhs.h"
#include "fhs_simple.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

const struct fhs_simple_def fhs_simple_nodes[] = {
	{
		.name = "root",
		.target = "/var/root",
		.state = "fhs.root",
		.summary = "The superuser's home directory",
		.creates_target = false,
	},
	{
		.name = "run",
		.target = "/var/run",
		.state = "fhs.run",
		.summary = "Runtime state - pid files and sockets",
		.creates_target = false,
	},
	{
		.name = "srv",
		.target = FHS_DATA_ROOT "/srv",
		.state = "fhs.srv",
		.summary = "Data served by this system; empty, as on Linux",
		.creates_target = true,
	},
};

const size_t fhs_simple_node_count =
    sizeof(fhs_simple_nodes) / sizeof(fhs_simple_nodes[0]);

const struct fhs_simple_def *
fhs_simple_find(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;

	/* Accept "/run" as well as "run". */
	if (name[0] == '/')
		name++;

	for (size_t i = 0; i < fhs_simple_node_count; i++) {
		if (strcmp(fhs_simple_nodes[i].name, name) == 0)
			return &fhs_simple_nodes[i];
	}

	return NULL;
}

int
fhs_simple_status(const struct fhs_simple_def *def, struct fhs_simple_status *st)
{
	if (def == NULL || st == NULL) {
		errno = EINVAL;
		return -1;
	}

	memset(st, 0, sizeof(*st));

	st->enabled = fhs_state_get(def->state, 0) != 0;

	if (fhs_skeleton_status_at(def->name, def->target, &st->skel) != 0)
		return -1;

	st->reboot_pending =
	    fhs_skeleton_reboot_pending_at(def->name, def->target);

	/*
	 * A missing target is worth reporting separately from a missing entry.
	 * For /root and /run it would mean a macOS directory has gone, which is
	 * not something this layer caused or can fix, and the symlink at / would
	 * dangle rather than simply be absent.
	 */
	st->target_missing = access(def->target, F_OK) != 0;

	return 0;
}

int
fhs_simple_enable(const struct fhs_simple_def *def)
{
	int rc;

	if (def == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (!fhs_is_root()) {
		fhs_err("enabling /%s requires root", def->name);
		return -1;
	}

	rc = fhs_skeleton_add_at(def->name, def->target, def->creates_target);
	if (rc < 0)
		return -1;

	if (fhs_state_set(def->state, 1) != 0)
		fhs_err("warning: could not persist state: %s", strerror(errno));

	if (fhs_skeleton_reboot_pending_at(def->name, def->target))
		fhs_log("/%s will appear after the next reboot.", def->name);
	else if (rc == 0)
		fhs_log("/%s is already present.", def->name);

	return 0;
}

int
fhs_simple_disable(const struct fhs_simple_def *def)
{
	int rc;

	if (def == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (!fhs_is_root()) {
		fhs_err("disabling /%s requires root", def->name);
		return -1;
	}

	rc = fhs_skeleton_remove_at(def->name, def->target);
	if (rc < 0)
		return -1;

	if (fhs_state_set(def->state, 0) != 0)
		fhs_err("warning: could not persist state: %s", strerror(errno));

	if (rc == 1)
		fhs_log("/%s will disappear after the next reboot.", def->name);

	return 0;
}
