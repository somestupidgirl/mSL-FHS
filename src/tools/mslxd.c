/*
 * Copyright (c) 2026 Sunneva N. Mariu
 *
 * mslxd.c
 *
 * The mSL/XNU layout daemon.
 *
 * Everything the layer does can be driven by hand with mslctl; this daemon
 * exists to do it at the right moments. It has three jobs:
 *
 *   Restore state at boot. Component enable flags live in /var/db/msl.*, and
 *   the symlink farms have to be rebuilt to match whatever is mounted and
 *   whichever accounts exist now.
 *
 *   Track volumes as they come and go. Without this, every eject leaves a
 *   dangling link in /media until someone runs `mslctl media sync` by hand -
 *   which is not an edge case but the normal result of unmounting anything.
 *
 *   Follow the console user. /media is attributed per user, so a login, logout
 *   or fast-user-switch changes which directory volumes belong in.
 *
 * It deliberately holds no state of its own. Every wakeup re-reads the world
 * and reconciles, so a missed event is corrected by the next one rather than
 * leaving the daemon's idea of the system permanently out of step. That also
 * means mslctl and mslxd cannot disagree: both call the same sync functions.
 *
 * Run as a root LaunchDaemon (KeepAlive), mirroring procfsd.
 */
#include "msl.h"
#include "msl_boot.h"
#include "msl_home.h"
#include "msl_media.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <SystemConfiguration/SystemConfiguration.h>

#ifndef MSL_VERSION
#define MSL_VERSION "0.0.0"
#endif

/*
 * Mounting one volume produces several DiskArbitration callbacks - appeared,
 * then one or more description changes as the mount completes. Reconciling on
 * each would rebuild the farm several times over and could observe the volume
 * mid-mount, before its path is set. Instead every event schedules a single
 * short-delay timer, so a burst collapses into one reconcile after things have
 * settled.
 */
#define COALESCE_SECONDS    1.0

/*
 * A slow periodic reconcile. DiskArbitration covers volumes and
 * SCDynamicStore covers the console user, but nothing notifies us when an
 * account is created or deleted, which /home cares about. Polling is
 * unglamorous and entirely adequate: the work is a directory scan against a
 * passwd enumeration, and being a minute late to notice a new account has no
 * consequence.
 */
#define PERIODIC_SECONDS    60.0

/*
 * An extra reconcile shortly after startup.
 *
 * At boot the daemon can be running before the rest of the system is ready to
 * answer it - in particular before opendirectoryd will return local accounts,
 * without which /home cannot be populated. The startup reconcile then does
 * nothing, and the periodic one is up to a minute away, leaving /home missing
 * for that whole window. A short follow-up closes it. Both reconciles are
 * idempotent, so an unnecessary one costs nothing.
 */
#define SETTLE_SECONDS      15.0

static CFRunLoopTimerRef s_coalesce;    /* non-NULL while a reconcile is pending */

/* ------------------------------------------------------------------------- */

/*
 * Reconcile every enabled component. Each sync is a no-op when its component
 * is disabled, so this needs no knowledge of which are on.
 */
static void
reconcile(const char *why)
{
	msl_log("mslxd: reconciling (%s)", why);

	if (msl_home_sync() != 0)
		msl_err("mslxd: /home sync failed");

	if (msl_media_sync() != 0)
		msl_err("mslxd: /media sync failed");

	/*
	 * /boot changes only when macOS is updated, but that is exactly when its
	 * links would otherwise be left pointing at a kernel version that no
	 * longer exists.
	 */
	if (msl_boot_sync() != 0)
		msl_err("mslxd: /boot sync failed");
}

static void
coalesce_fired(CFRunLoopTimerRef timer, void *info)
{
	s_coalesce = NULL;
	reconcile((const char *)info);
}

/*
 * Ask for a reconcile shortly from now. Repeated calls before it fires are
 * absorbed into the one pending timer.
 */
static void
schedule_reconcile(const char *why)
{
	CFRunLoopTimerContext ctx = { 0, (void *)why, NULL, NULL, NULL };

	if (s_coalesce != NULL)
		return;

	s_coalesce = CFRunLoopTimerCreate(kCFAllocatorDefault,
	    CFAbsoluteTimeGetCurrent() + COALESCE_SECONDS, 0, 0, 0,
	    coalesce_fired, &ctx);

	if (s_coalesce == NULL) {
		/* Out of memory for a timer: do the work now rather than lose it. */
		reconcile(why);
		return;
	}

	CFRunLoopAddTimer(CFRunLoopGetCurrent(), s_coalesce, kCFRunLoopDefaultMode);
	CFRelease(s_coalesce);	/* the run loop retains it */
}

/* ------------------------------------------------------------------------- */

static void
disk_appeared(DADiskRef disk, void *ctx)
{
	schedule_reconcile("volume appeared");
}

static void
disk_disappeared(DADiskRef disk, void *ctx)
{
	schedule_reconcile("volume disappeared");
}

static void
disk_changed(DADiskRef disk, CFArrayRef keys, void *ctx)
{
	/*
	 * Fires for renames and for the mount path being set after a volume
	 * appears - both of which change what should be in /media.
	 */
	schedule_reconcile("volume changed");
}

static void
console_user_changed(SCDynamicStoreRef store, CFArrayRef keys, void *info)
{
	schedule_reconcile("console user changed");
}

static void
periodic_fired(CFRunLoopTimerRef timer, void *info)
{
	reconcile("periodic");
}

static void
settle_fired(CFRunLoopTimerRef timer, void *info)
{
	reconcile("post-boot settle");
}

/* ------------------------------------------------------------------------- */

static void
on_signal(int sig)
{
	/*
	 * Async-signal-safe: just stop the run loop. Nothing needs tearing down -
	 * the daemon owns no state, and the symlinks it maintains are meant to
	 * outlive it.
	 */
	CFRunLoopStop(CFRunLoopGetMain());
}

static bool
watch_volumes(DASessionRef session)
{
	DARegisterDiskAppearedCallback(session, NULL, disk_appeared, NULL);
	DARegisterDiskDisappearedCallback(session, NULL, disk_disappeared, NULL);
	DARegisterDiskDescriptionChangedCallback(session, NULL, NULL, disk_changed, NULL);

	DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(),
	    kCFRunLoopDefaultMode);

	return true;
}

static bool
watch_console_user(void)
{
	SCDynamicStoreRef store;
	CFStringRef key;
	CFMutableArrayRef keys;
	CFRunLoopSourceRef source;
	bool ok = false;

	store = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("mslxd"),
	    console_user_changed, NULL);
	if (store == NULL)
		return false;

	key = SCDynamicStoreKeyCreateConsoleUser(kCFAllocatorDefault);
	if (key == NULL) {
		CFRelease(store);
		return false;
	}

	keys = CFArrayCreateMutable(kCFAllocatorDefault, 1, &kCFTypeArrayCallBacks);
	if (keys != NULL) {
		CFArrayAppendValue(keys, key);
		if (SCDynamicStoreSetNotificationKeys(store, keys, NULL)) {
			source = SCDynamicStoreCreateRunLoopSource(kCFAllocatorDefault,
			    store, 0);
			if (source != NULL) {
				CFRunLoopAddSource(CFRunLoopGetCurrent(), source,
				    kCFRunLoopDefaultMode);
				CFRelease(source);
				ok = true;
			}
		}
		CFRelease(keys);
	}

	CFRelease(key);

	/*
	 * `store` is deliberately not released: the run loop source holds the
	 * callback, and the daemon runs until it is killed.
	 */
	return ok;
}

int
main(int argc, char **argv)
{
	DASessionRef session;
	CFRunLoopTimerRef periodic, settle;

	if (argc > 1 && strcmp(argv[1], "--version") == 0) {
		printf("mslxd %s\n", MSL_VERSION);
		return 0;
	}

	if (!msl_is_root()) {
		msl_err("mslxd must run as root");
		return 1;
	}

	setvbuf(stderr, NULL, _IOLBF, 0);	/* line-buffered for the log file */
	msl_log("mslxd %s starting", MSL_VERSION);

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);

	/*
	 * Reconcile once at startup, before any events. This is the boot-time
	 * restore: whatever was enabled comes back, matched to the volumes and
	 * accounts that exist now rather than the ones that existed at shutdown.
	 */
	reconcile("startup");

	session = DASessionCreate(kCFAllocatorDefault);
	if (session == NULL) {
		msl_err("mslxd: cannot create a DiskArbitration session");
		return 1;
	}

	watch_volumes(session);

	if (!watch_console_user()) {
		/*
		 * Not fatal. Without it, a fast user switch is picked up by the
		 * periodic reconcile instead of immediately.
		 */
		msl_err("mslxd: cannot watch the console user; "
		    "falling back to periodic reconcile");
	}

	periodic = CFRunLoopTimerCreate(kCFAllocatorDefault,
	    CFAbsoluteTimeGetCurrent() + PERIODIC_SECONDS, PERIODIC_SECONDS,
	    0, 0, periodic_fired, NULL);
	if (periodic != NULL) {
		CFRunLoopAddTimer(CFRunLoopGetCurrent(), periodic,
		    kCFRunLoopDefaultMode);
		CFRelease(periodic);
	}

	/* One-shot follow-up, for the boot case described at SETTLE_SECONDS. */
	settle = CFRunLoopTimerCreate(kCFAllocatorDefault,
	    CFAbsoluteTimeGetCurrent() + SETTLE_SECONDS, 0, 0, 0,
	    settle_fired, NULL);
	if (settle != NULL) {
		CFRunLoopAddTimer(CFRunLoopGetCurrent(), settle, kCFRunLoopDefaultMode);
		CFRelease(settle);
	}

	msl_log("mslxd: watching for volume and session changes");
	CFRunLoopRun();

	msl_log("mslxd: stopping");
	DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(),
	    kCFRunLoopDefaultMode);
	CFRelease(session);

	return 0;
}
