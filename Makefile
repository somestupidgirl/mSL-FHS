#
# mSL/XNU - macOS Subsystem for Linux / X is Now UNIX
#
# Usage:
#   make                    # build everything into $(OUT)
#   make ARCH=universal     # fat binary (arm64 + x86_64)
#   sudo make install       # install into the system (run AFTER make)
#   sudo make uninstall     # remove from the system
#   make clean              # remove build artifacts (no sudo needed)
#
# NOTE: never build as root. `make install` only COPIES already-built artifacts
# from $(OUT), so every build artifact stays owned by the invoking user and
# `make clean` never needs sudo.
#
OUT      := out
SRC      := src
SBIN_DIR := /usr/local/sbin

VERSION  := $(strip $(shell cat VERSION 2>/dev/null || echo 0.0.0))

NATIVE_ARCH := $(shell uname -m)
ARCH ?= $(NATIVE_ARCH)

ifeq ($(ARCH),universal)
    ARCHFLAGS := -arch arm64 -arch x86_64
else
    ARCHFLAGS := -arch $(ARCH)
endif

CC       ?= /usr/bin/cc
CFLAGS   := $(ARCHFLAGS) -std=c11 -O2 -g \
            -Wall -Wextra -Wshadow -Wpointer-arith -Wwrite-strings \
            -Wno-unused-parameter \
            -mmacosx-version-min=15.0 \
            -DMSL_VERSION=\"$(VERSION)\" \
            -I$(SRC)/include

MSLCTL_SRCS := $(SRC)/common/msl_util.c \
               $(SRC)/skeleton/msl_skeleton.c \
               $(SRC)/home/msl_home.c \
               $(SRC)/mnt/msl_mnt.c \
               $(SRC)/media/msl_media.c \
               $(SRC)/tools/mslctl.c
MSLCTL      := $(OUT)/mslctl

MSLXD_SRCS  := $(SRC)/common/msl_util.c \
               $(SRC)/skeleton/msl_skeleton.c \
               $(SRC)/home/msl_home.c \
               $(SRC)/media/msl_media.c \
               $(SRC)/tools/mslxd.c
MSLXD       := $(OUT)/mslxd

DAEMON_PLIST := com.beako.mslxd.plist
DAEMON_LABEL := com.beako.mslxd
DAEMON_DIR   := /Library/LaunchDaemons

# /media reads device properties from DiskArbitration and the console user from
# SystemConfiguration; neither has a POSIX equivalent on macOS.
FRAMEWORKS  := -framework CoreFoundation \
               -framework DiskArbitration \
               -framework SystemConfiguration

all: $(MSLCTL) $(MSLXD) $(OUT)/$(DAEMON_PLIST)

$(MSLCTL): $(MSLCTL_SRCS) $(wildcard $(SRC)/include/*.h) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(MSLCTL_SRCS) $(FRAMEWORKS)

$(MSLXD): $(MSLXD_SRCS) $(wildcard $(SRC)/include/*.h) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(MSLXD_SRCS) $(FRAMEWORKS)

$(OUT)/$(DAEMON_PLIST): $(SRC)/tools/$(DAEMON_PLIST) | $(OUT)
	cp $< $@

$(OUT):
	@mkdir -p $(OUT)

# ---------------------------------------------------------------------------
# Tests
#
# The auto_master rewrite is the one piece of this component that edits a system
# file governing login, so it is tested against copies in a scratch directory:
# the paths it uses are redirected with -D, and the live configuration is never
# read or written. Needs no root.
# ---------------------------------------------------------------------------

TESTDIR := $(OUT)/testdata

check: | $(OUT)
	@mkdir -p $(TESTDIR)
	$(CC) $(CFLAGS) -I$(SRC)/home \
	    -DAUTO_MASTER='"$(TESTDIR)/auto_master"' \
	    -DAUTO_HOME='"$(TESTDIR)/auto_home"' \
	    -DAUTO_BACKUP='"$(TESTDIR)/auto_master.orig"' \
	    -o $(OUT)/test_auto_master $(SRC)/common/msl_util.c tests/test_auto_master.c
	$(CC) $(CFLAGS) -I$(SRC)/skeleton \
	    -DSYNTHETIC_CONF='"$(TESTDIR)/synthetic.conf"' \
	    -o $(OUT)/test_synthetic $(SRC)/common/msl_util.c tests/test_synthetic.c
	$(CC) $(CFLAGS) -I$(SRC)/media \
	    -DMEDIA_ROOT='"$(TESTDIR)/media"' \
	    -o $(OUT)/test_media $(SRC)/common/msl_util.c $(SRC)/skeleton/msl_skeleton.c \
	    tests/test_media.c $(FRAMEWORKS)
	@echo "==> auto_master rewrite tests"
	@$(OUT)/test_auto_master
	@echo
	@echo "==> synthetic.conf rewrite tests"
	@$(OUT)/test_synthetic
	@echo
	@echo "==> media label tests"
	@$(OUT)/test_media

# ---------------------------------------------------------------------------
# Install / uninstall
# ---------------------------------------------------------------------------

install: require-root require-built
	install -d -m 755 -o root -g wheel $(SBIN_DIR)
	install -m 755 -o root -g wheel $(MSLCTL) $(SBIN_DIR)/mslctl
	install -m 755 -o root -g wheel $(MSLXD)  $(SBIN_DIR)/mslxd
	install -m 644 -o root -g wheel $(OUT)/$(DAEMON_PLIST) $(DAEMON_DIR)/$(DAEMON_PLIST)
	@# A prior `launchctl disable` persists across boots in the override store
	@# and would otherwise keep mslxd from ever starting.
	-@launchctl enable system/$(DAEMON_LABEL) 2>/dev/null || true
	-@launchctl bootout system/$(DAEMON_LABEL) 2>/dev/null || true
	launchctl bootstrap system $(DAEMON_DIR)/$(DAEMON_PLIST)
	@echo "mSL: installed mslctl and mslxd to $(SBIN_DIR); daemon started."
	@echo "mSL: nothing is enabled yet. To turn on the /home component:"
	@echo "         sudo mslctl home check     # confirm it is safe"
	@echo "         sudo mslctl home enable"

# Turn the layer off before removing the tool that knows how to turn it off:
# leaving a masked /etc/auto_master behind with no way to restore it would be a
# hostile way to uninstall.
uninstall: require-root
	-@launchctl bootout system/$(DAEMON_LABEL) 2>/dev/null || true
	-@[ -x $(SBIN_DIR)/mslctl ] && $(SBIN_DIR)/mslctl media disable || true
	-@[ -x $(SBIN_DIR)/mslctl ] && $(SBIN_DIR)/mslctl mnt disable   || true
	-@[ -x $(SBIN_DIR)/mslctl ] && $(SBIN_DIR)/mslctl home disable  || true
	rm -f $(DAEMON_DIR)/$(DAEMON_PLIST)
	rm -f $(SBIN_DIR)/mslctl $(SBIN_DIR)/mslxd
	rm -f /var/db/msl.home /var/db/msl.mnt /var/db/msl.media
	@echo "mSL: uninstalled. /var/db/msl.auto_master.orig kept as a backup."

require-root:
	@[ "$$(id -u)" -eq 0 ] || \
		{ echo "error: run as root (sudo make $(MAKECMDGOALS))"; exit 1; }

require-built:
	@[ -x "$(MSLCTL)" ] && [ -x "$(MSLXD)" ] || \
		{ echo "error: not built. Run 'make' first."; exit 1; }

clean:
	rm -rf $(OUT)

.PHONY: all check install uninstall require-root require-built clean
