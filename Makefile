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
               $(SRC)/common/msl_detect.c \
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

APP_DIR      := /Applications
PREFPANE_DIR := /Library/PreferencePanes
DAEMON_PLIST := com.beako.mslxd.plist
DAEMON_LABEL := com.beako.mslxd
DAEMON_DIR   := /Library/LaunchDaemons

# /media reads device properties from DiskArbitration and the console user from
# SystemConfiguration; neither has a POSIX equivalent on macOS.
FRAMEWORKS  := -framework CoreFoundation \
               -framework DiskArbitration \
               -framework SystemConfiguration

all: $(MSLCTL) $(MSLXD) $(OUT)/$(DAEMON_PLIST) gui

$(MSLCTL): $(MSLCTL_SRCS) $(wildcard $(SRC)/include/*.h) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(MSLCTL_SRCS) $(FRAMEWORKS)

$(MSLXD): $(MSLXD_SRCS) $(wildcard $(SRC)/include/*.h) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(MSLXD_SRCS) $(FRAMEWORKS)

$(OUT)/$(DAEMON_PLIST): $(SRC)/tools/$(DAEMON_PLIST) | $(OUT)
	cp $< $@

# Menu-bar app and preference pane.
gui: | $(OUT)
	$(MAKE) -C $(SRC)/gui
	rm -rf $(OUT)/mSL.app $(OUT)/mSL.prefPane
	mv $(SRC)/gui/mSL.app $(SRC)/gui/mSL.prefPane $(OUT)/

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
# Distribution: a double-clickable installer (.pkg) and disk image (.dmg).
# These stage and package the already-built artifacts; nothing is installed on
# the build host, so no root is needed. The .pkg's postinstall does the system
# setup at install time on the target machine.
# ---------------------------------------------------------------------------

PKG_ID   := com.beako.msl.pkg
PKG_COMP := $(OUT)/msl-component.pkg
PKG_OUT  := $(OUT)/mSL-XNU-$(VERSION).pkg
DMG_OUT  := $(OUT)/mSL-XNU-$(VERSION).dmg

pkg: all
	@echo "==> Staging installer payload"
	rm -rf $(OUT)/pkgroot $(OUT)/pkgres
	install -d $(OUT)/pkgroot/usr/local/sbin \
	           $(OUT)/pkgroot/Library/LaunchDaemons \
	           $(OUT)/pkgroot/Applications \
	           $(OUT)/pkgroot/Library/PreferencePanes
	cp    $(MSLCTL) $(MSLXD)          $(OUT)/pkgroot/usr/local/sbin/
	cp    $(OUT)/$(DAEMON_PLIST)      $(OUT)/pkgroot/Library/LaunchDaemons/
	cp -R $(OUT)/mSL.app              $(OUT)/pkgroot/Applications/
	cp -R $(OUT)/mSL.prefPane         $(OUT)/pkgroot/Library/PreferencePanes/
	@# codesign and pkgbuild reject Finder-info and similar xattrs.
	xattr -cr $(OUT)/pkgroot
	@echo "==> Building component package"
	pkgbuild --root $(OUT)/pkgroot --identifier $(PKG_ID) --version $(VERSION) \
	         --scripts installer/scripts --ownership recommended \
	         --component-plist installer/msl-component.plist \
	         --install-location / $(PKG_COMP)
	@echo "==> Building product archive"
	mkdir -p $(OUT)/pkgres
	cp installer/resources/welcome.html installer/resources/conclusion.html $(OUT)/pkgres/
	cp LICENSE $(OUT)/pkgres/LICENSE
	sed -e 's/__MSLVERSION__/$(VERSION)/g' installer/distribution.xml.in > $(OUT)/distribution.xml
	productbuild --distribution $(OUT)/distribution.xml --package-path $(OUT) \
	             --resources $(OUT)/pkgres $(PKG_OUT)
	rm -rf $(PKG_COMP) $(OUT)/distribution.xml $(OUT)/pkgroot $(OUT)/pkgres
	@echo "==> Built $(PKG_OUT)"

dmg: pkg
	@echo "==> Building disk image"
	rm -f $(DMG_OUT)
	rm -rf $(OUT)/dmg
	mkdir -p $(OUT)/dmg
	cp $(PKG_OUT) $(OUT)/dmg/
	cp installer/resources/DMG-README.txt $(OUT)/dmg/README.txt
	cp installer/uninstall.command "$(OUT)/dmg/Uninstall mSL.command"
	chmod +x "$(OUT)/dmg/Uninstall mSL.command"
	hdiutil create -volname "mSL-XNU $(VERSION)" -srcfolder $(OUT)/dmg \
	               -ov -format UDZO $(DMG_OUT)
	rm -rf $(OUT)/dmg
	@echo "==> Built $(DMG_OUT)"

# Distribution sanity check: a clean build of the installer artifacts, then
# verify both were produced and that the package payload carries every
# component. Catches a payload silently losing a binary far more reliably than
# noticing it is missing after an install.
distcheck:
	@echo "==> Clean distribution build"
	$(MAKE) clean
	$(MAKE) dmg
	@echo "==> Checking distribution artifacts"
	@test -s "$(PKG_OUT)" || { echo "FAIL: $(PKG_OUT) missing or empty"; exit 1; }
	@test -s "$(DMG_OUT)" || { echo "FAIL: $(DMG_OUT) missing or empty"; exit 1; }
	@hdiutil imageinfo "$(DMG_OUT)" >/dev/null 2>&1 || \
		{ echo "FAIL: $(DMG_OUT) is not a valid disk image"; exit 1; }
	@echo "  ok  built $(notdir $(PKG_OUT)) and $(notdir $(DMG_OUT))"
	@rm -rf $(OUT)/distcheck; pkgutil --expand "$(PKG_OUT)" $(OUT)/distcheck 2>/dev/null || \
		{ echo "FAIL: cannot expand product archive"; exit 1; }
	@bom=`find $(OUT)/distcheck -name Bom | head -1`; \
	 test -n "$$bom" || { echo "FAIL: no component package (Bom) in archive"; exit 1; }; \
	 for f in mslctl mslxd $(DAEMON_PLIST) mSL.app mSL.prefPane; do \
	   lsbom "$$bom" 2>/dev/null | grep -q "$$f" || \
	     { echo "FAIL: payload missing $$f"; rm -rf $(OUT)/distcheck; exit 1; }; \
	   echo "  ok  payload: $$f"; \
	 done
	@rm -rf $(OUT)/distcheck
	@echo "==> distcheck passed"

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
	rm -rf $(APP_DIR)/mSL.app $(PREFPANE_DIR)/mSL.prefPane
	cp -R $(OUT)/mSL.app $(APP_DIR)/mSL.app
	cp -R $(OUT)/mSL.prefPane $(PREFPANE_DIR)/mSL.prefPane
	chown -R root:wheel $(APP_DIR)/mSL.app $(PREFPANE_DIR)/mSL.prefPane
	chmod -R 755 $(APP_DIR)/mSL.app $(PREFPANE_DIR)/mSL.prefPane
	@# Gatekeeper flags a quarantined bundle as damaged when it arrives via a
	@# download; the build products are local, but the payload may not be.
	-@xattr -dr com.apple.quarantine $(APP_DIR)/mSL.app 2>/dev/null || true
	@# Launch the menu-bar app in the console user's session so its icon shows
	@# immediately. Best-effort: root install hopping to the logged-in user.
	-@u=$$(stat -f '%Su' /dev/console 2>/dev/null); \
	  uid=$$(id -u "$$u" 2>/dev/null); \
	  if [ -n "$$uid" ] && [ "$$u" != "root" ] && [ "$$u" != "loginwindow" ]; then \
	      launchctl asuser "$$uid" sudo -u "$$u" open "$(APP_DIR)/mSL.app" >/dev/null 2>&1 || true; \
	  fi
	@echo "mSL: installed mslctl, mslxd, mSL.app and mSL.prefPane; daemon started."
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
	rm -rf $(APP_DIR)/mSL.app $(PREFPANE_DIR)/mSL.prefPane
	rm -f /var/db/msl.home /var/db/msl.mnt /var/db/msl.media
	@echo "mSL: uninstalled. /var/db/msl.auto_master.orig kept as a backup."

require-root:
	@[ "$$(id -u)" -eq 0 ] || \
		{ echo "error: run as root (sudo make $(MAKECMDGOALS))"; exit 1; }

require-built:
	@[ -x "$(MSLCTL)" ] && [ -x "$(MSLXD)" ] && [ -d "$(OUT)/mSL.app" ] || \
		{ echo "error: not built. Run 'make' first."; exit 1; }

clean:
	rm -rf $(OUT)
	$(MAKE) -C $(SRC)/gui clean

.PHONY: all gui check pkg dmg distcheck install uninstall require-root require-built clean
