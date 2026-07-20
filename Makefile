#
# mSL/FHS - macOS Subsystem for Linux / X is Now UNIX
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
            -DFHS_VERSION=\"$(VERSION)\" \
            -I$(SRC)/include

FHSCTL_SRCS := $(SRC)/common/fhs_util.c \
               $(SRC)/common/fhs_detect.c \
               $(SRC)/visibility/fhs_visibility.c \
               $(SRC)/skeleton/fhs_skeleton.c \
               $(SRC)/home/fhs_home.c \
               $(SRC)/mnt/fhs_mnt.c \
               $(SRC)/boot/fhs_boot.c \
               $(SRC)/simple/fhs_simple.c \
               $(SRC)/media/fhs_media.c \
               $(SRC)/tools/fhsctl.c
FHSCTL      := $(OUT)/fhsctl

FHSXD_SRCS  := $(SRC)/common/fhs_util.c \
               $(SRC)/skeleton/fhs_skeleton.c \
               $(SRC)/boot/fhs_boot.c \
               $(SRC)/home/fhs_home.c \
               $(SRC)/media/fhs_media.c \
               $(SRC)/tools/fhsxd.c
FHSXD       := $(OUT)/fhsxd

# The modules share a folder rather than scattering apps across /Applications:
# mSL/FHS is one of several planned mSL components, and they belong together.
APP_ROOT     := /Applications/mSL
APP_DIR      := $(APP_ROOT)
PREFPANE_DIR := /Library/PreferencePanes
DAEMON_PLIST := com.beako.fhsxd.plist
DAEMON_LABEL := com.beako.fhsxd
DAEMON_DIR   := /Library/LaunchDaemons

# /media reads device properties from DiskArbitration and the console user from
# SystemConfiguration; neither has a POSIX equivalent on macOS.
FRAMEWORKS  := -framework CoreFoundation \
               -framework DiskArbitration \
               -framework SystemConfiguration

all: $(FHSCTL) $(FHSXD) $(OUT)/$(DAEMON_PLIST) gui

$(FHSCTL): $(FHSCTL_SRCS) $(wildcard $(SRC)/include/*.h) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(FHSCTL_SRCS) $(FRAMEWORKS)

$(FHSXD): $(FHSXD_SRCS) $(wildcard $(SRC)/include/*.h) | $(OUT)
	$(CC) $(CFLAGS) -o $@ $(FHSXD_SRCS) $(FRAMEWORKS)

$(OUT)/$(DAEMON_PLIST): $(SRC)/tools/$(DAEMON_PLIST) | $(OUT)
	cp $< $@

# Menu-bar app and preference pane.
gui: | $(OUT)
	$(MAKE) -C $(SRC)/gui
	rm -rf $(OUT)/FHS.app $(OUT)/FHS.prefPane
	mv $(SRC)/gui/FHS.app $(SRC)/gui/FHS.prefPane $(OUT)/

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
	    -o $(OUT)/test_auto_master $(SRC)/common/fhs_util.c \
	    $(SRC)/skeleton/fhs_skeleton.c tests/test_auto_master.c
	$(CC) $(CFLAGS) -I$(SRC)/skeleton \
	    -DSYNTHETIC_CONF='"$(TESTDIR)/synthetic.conf"' \
	    -o $(OUT)/test_synthetic $(SRC)/common/fhs_util.c tests/test_synthetic.c
	$(CC) $(CFLAGS) -I$(SRC)/media \
	    -DMEDIA_ROOT='"$(TESTDIR)/media"' \
	    -o $(OUT)/test_media $(SRC)/common/fhs_util.c $(SRC)/skeleton/fhs_skeleton.c \
	    tests/test_media.c $(FRAMEWORKS)
	$(CC) $(CFLAGS) -I$(SRC)/visibility \
	    -DTEST_SCRATCH='"$(TESTDIR)/vis"' \
	    -o $(OUT)/test_visibility $(SRC)/common/fhs_util.c tests/test_visibility.c
	$(CC) $(CFLAGS) -I$(SRC)/mnt \
	    -o $(OUT)/test_mnt $(SRC)/common/fhs_util.c $(SRC)/skeleton/fhs_skeleton.c \
	    tests/test_mnt.c
	$(CC) $(CFLAGS) -I$(SRC)/boot \
	    -DTEST_SCRATCH='"$(TESTDIR)/boot"' \
	    -o $(OUT)/test_boot $(SRC)/common/fhs_util.c $(SRC)/skeleton/fhs_skeleton.c \
	    tests/test_boot.c
	$(CC) $(CFLAGS) -I$(SRC)/simple \
	    -o $(OUT)/test_simple $(SRC)/common/fhs_util.c $(SRC)/skeleton/fhs_skeleton.c \
	    tests/test_simple.c
	@echo "==> auto_master rewrite tests"
	@$(OUT)/test_auto_master
	@echo
	@echo "==> synthetic.conf rewrite tests"
	@$(OUT)/test_synthetic
	@echo
	@echo "==> media label tests"
	@$(OUT)/test_media
	@echo
	@echo "==> visibility tests"
	@$(OUT)/test_visibility
	@echo
	@echo "==> /mnt mount-matcher tests"
	@$(OUT)/test_mnt
	@echo
	@echo "==> skeleton-only node tests"
	@$(OUT)/test_simple
	@echo
	@echo "==> /boot tests"
	@$(OUT)/test_boot

# ---------------------------------------------------------------------------
# Distribution: a double-clickable installer (.pkg) and disk image (.dmg).
# These stage and package the already-built artifacts; nothing is installed on
# the build host, so no root is needed. The .pkg's postinstall does the system
# setup at install time on the target machine.
# ---------------------------------------------------------------------------

PKG_ID   := com.beako.fhs.pkg
PKG_COMP := $(OUT)/fhs-component.pkg
PKG_OUT  := $(OUT)/mSL-XNU-$(VERSION).pkg
DMG_OUT  := $(OUT)/mSL-XNU-$(VERSION).dmg

pkg: all
	@echo "==> Staging installer payload"
	rm -rf $(OUT)/pkgroot $(OUT)/pkgres
	install -d $(OUT)/pkgroot/usr/local/sbin \
	           $(OUT)/pkgroot/Library/LaunchDaemons \
	           $(OUT)/pkgroot/Applications/mSL \
	           $(OUT)/pkgroot/Library/PreferencePanes
	cp    $(FHSCTL) $(FHSXD)          $(OUT)/pkgroot/usr/local/sbin/
	cp    $(OUT)/$(DAEMON_PLIST)      $(OUT)/pkgroot/Library/LaunchDaemons/
	cp -R $(OUT)/FHS.app              $(OUT)/pkgroot/Applications/mSL/
	cp -R $(OUT)/FHS.prefPane         $(OUT)/pkgroot/Library/PreferencePanes/
	@# codesign and pkgbuild reject Finder-info and similar xattrs.
	xattr -cr $(OUT)/pkgroot
	@echo "==> Building component package"
	pkgbuild --root $(OUT)/pkgroot --identifier $(PKG_ID) --version $(VERSION) \
	         --scripts installer/scripts --ownership recommended \
	         --component-plist installer/fhs-component.plist \
	         --install-location / $(PKG_COMP)
	@echo "==> Building product archive"
	mkdir -p $(OUT)/pkgres
	cp installer/resources/welcome.html installer/resources/conclusion.html $(OUT)/pkgres/
	cp LICENSE $(OUT)/pkgres/LICENSE
	sed -e 's/__FHSVERSION__/$(VERSION)/g' installer/distribution.xml.in > $(OUT)/distribution.xml
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
	 for f in fhsctl fhsxd $(DAEMON_PLIST) FHS.app FHS.prefPane; do \
	   lsbom "$$bom" 2>/dev/null | grep -q "$$f" || \
	     { echo "FAIL: payload missing $$f"; rm -rf $(OUT)/distcheck; exit 1; }; \
	   echo "  ok  payload: $$f"; \
	 done
	@rm -rf $(OUT)/distcheck
	@echo "==> distcheck passed"

# ---------------------------------------------------------------------------
# Install / uninstall
# ---------------------------------------------------------------------------

install: require-root require-built migrate
	install -d -m 755 -o root -g wheel $(SBIN_DIR)

# ---------------------------------------------------------------------------
# Migration from the pre-rename layout (mSL/FHS, before the project became the
# mSL/FHS module).
#
# Everything the layer configures survives the rename untouched: the
# /etc/synthetic.conf entries are named for the directories themselves, and the
# /etc/auto_master marker is still recognised in its old spelling. What does
# have to move is the daemon and the persisted state, since both carry the old
# name - and if the state files were left behind, the new build would read every
# component as disabled and quietly stop maintaining a layout that is still
# live at /.
#
# Idempotent, and silent when there is nothing from the old layout to move.
# ---------------------------------------------------------------------------
migrate: require-root
	@# The old daemon must stop before its state is renamed underneath it.
	-@if launchctl print system/com.beako.mslxd >/dev/null 2>&1; then \
		echo "mSL: stopping the pre-rename daemon"; \
		launchctl bootout system/com.beako.mslxd 2>/dev/null || true; \
	fi
	-@rm -f /Library/LaunchDaemons/com.beako.mslxd.plist
	-@for old in /var/db/msl.*; do \
		[ -e "$$old" ] || continue; \
		new=`echo "$$old" | sed 's|/msl\.|/fhs.|'`; \
		if [ -e "$$new" ]; then rm -f "$$old"; else \
			echo "mSL: $$old -> $$new"; mv "$$old" "$$new"; fi; \
	done
	-@rm -f $(SBIN_DIR)/mslctl $(SBIN_DIR)/mslxd
	@# The bundles were renamed too; leaving the old ones behind would put two
	@# copies of the same app in /Applications, one of them calling tools that
	@# no longer exist.
	@# The app has moved twice: it was /Applications/mSL.app before the rename,
	@# then briefly a flat /Applications/FHS.app. Either left behind would put a
	@# second copy in the Finder, one of them calling tools that no longer exist.
	-@for stale in /Applications/mSL.app /Applications/FHS.app; do \
		[ -d "$$stale" ] || continue; \
		echo "mSL: removing the superseded $$stale"; \
		killall mSL FHS 2>/dev/null || true; \
		rm -rf "$$stale"; \
	done
	-@rm -rf $(PREFPANE_DIR)/mSL.prefPane
	install -m 755 -o root -g wheel $(FHSCTL) $(SBIN_DIR)/fhsctl
	install -m 755 -o root -g wheel $(FHSXD)  $(SBIN_DIR)/fhsxd
	install -m 644 -o root -g wheel $(OUT)/$(DAEMON_PLIST) $(DAEMON_DIR)/$(DAEMON_PLIST)
	@# A prior `launchctl disable` persists across boots in the override store
	@# and would otherwise keep fhsxd from ever starting.
	-@launchctl enable system/$(DAEMON_LABEL) 2>/dev/null || true
	@# Replace any running instance. bootout is asynchronous: launchd returns
	@# before the job is gone, and bootstrapping while the label is still known
	@# fails with EIO (5). So wait for it to actually disappear, and if it
	@# refuses to (a wedged job, or one launchd will not release), restart it in
	@# place instead - kickstart re-execs the program, which is the freshly
	@# installed binary at the same path.
	-@launchctl bootout system/$(DAEMON_LABEL) 2>/dev/null || true
	-@for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do \
		launchctl print system/$(DAEMON_LABEL) >/dev/null 2>&1 || break; \
		sleep 0.25; \
	done
	@launchctl bootstrap system $(DAEMON_DIR)/$(DAEMON_PLIST) 2>/dev/null \
		|| launchctl kickstart -k system/$(DAEMON_LABEL) \
		|| { echo "error: could not start $(DAEMON_LABEL)"; exit 1; }
	install -d -m 755 -o root -g wheel $(APP_ROOT)
	rm -rf $(APP_DIR)/FHS.app $(PREFPANE_DIR)/FHS.prefPane
	@# Remove the shared folder only if this was the last module in it.
	-@rmdir $(APP_ROOT) 2>/dev/null || true
	cp -R $(OUT)/FHS.app $(APP_DIR)/FHS.app
	cp -R $(OUT)/FHS.prefPane $(PREFPANE_DIR)/FHS.prefPane
	chown -R root:wheel $(APP_DIR)/FHS.app $(PREFPANE_DIR)/FHS.prefPane
	chmod -R 755 $(APP_DIR)/FHS.app $(PREFPANE_DIR)/FHS.prefPane
	@# Gatekeeper flags a quarantined bundle as damaged when it arrives via a
	@# download; the build products are local, but the payload may not be.
	-@xattr -dr com.apple.quarantine $(APP_DIR)/FHS.app 2>/dev/null || true
	@# Launch the menu-bar app in the console user's session so its icon shows
	@# immediately. Best-effort: root install hopping to the logged-in user.
	-@u=$$(stat -f '%Su' /dev/console 2>/dev/null); \
	  uid=$$(id -u "$$u" 2>/dev/null); \
	  if [ -n "$$uid" ] && [ "$$u" != "root" ] && [ "$$u" != "loginwindow" ]; then \
	      launchctl asuser "$$uid" sudo -u "$$u" open "$(APP_DIR)/FHS.app" >/dev/null 2>&1 || true; \
	  fi
	@echo "mSL: installed fhsctl, fhsxd, FHS.app and FHS.prefPane; daemon started."
	@# Only claim nothing is enabled when that is actually true. A reinstall
	@# over a configured system left the old message telling the user to turn
	@# on components they had already turned on.
	@if $(SBIN_DIR)/fhsctl porcelain 2>/dev/null | grep -q '\.enabled=1'; then \
		echo "mSL: your enabled components are unchanged. Review them with:"; \
		echo "         fhsctl status"; \
	else \
		echo "mSL: nothing is enabled yet. Turn components on in"; \
		echo "     System Settings -> mSL/FHS, or from the command line:"; \
		echo "         sudo fhsctl home check     # confirm /home is safe to enable"; \
		echo "         sudo fhsctl home enable"; \
	fi

# Turn the layer off before removing the tool that knows how to turn it off:
# leaving a masked /etc/auto_master behind with no way to restore it would be a
# hostile way to uninstall.
uninstall: require-root
	-@launchctl bootout system/$(DAEMON_LABEL) 2>/dev/null || true
	-@[ -x $(SBIN_DIR)/fhsctl ] && $(SBIN_DIR)/fhsctl media disable || true
	-@[ -x $(SBIN_DIR)/fhsctl ] && $(SBIN_DIR)/fhsctl mnt disable   || true
	-@[ -x $(SBIN_DIR)/fhsctl ] && $(SBIN_DIR)/fhsctl home disable  || true
	rm -f $(DAEMON_DIR)/$(DAEMON_PLIST)
	rm -f $(SBIN_DIR)/fhsctl $(SBIN_DIR)/fhsxd
	rm -rf $(APP_DIR)/FHS.app $(PREFPANE_DIR)/FHS.prefPane
	@# Remove the shared folder only if this was the last module in it.
	-@rmdir $(APP_ROOT) 2>/dev/null || true
	rm -f /var/db/fhs.home /var/db/fhs.mnt /var/db/fhs.media \
	      /var/db/fhs.root /var/db/fhs.run /var/db/fhs.srv /var/db/fhs.boot
	@echo "mSL: uninstalled. /var/db/fhs.auto_master.orig kept as a backup."

require-root:
	@[ "$$(id -u)" -eq 0 ] || \
		{ echo "error: run as root (sudo make $(MAKECMDGOALS))"; exit 1; }

require-built:
	@[ -x "$(FHSCTL)" ] && [ -x "$(FHSXD)" ] && [ -d "$(OUT)/FHS.app" ] || \
		{ echo "error: not built. Run 'make' first."; exit 1; }

clean:
	rm -rf $(OUT)
	$(MAKE) -C $(SRC)/gui clean

.PHONY: all gui check pkg dmg distcheck migrate install uninstall require-root require-built clean
