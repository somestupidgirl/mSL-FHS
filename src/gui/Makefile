# Builds the mSL/FHS menu-bar app (FHS.app) and preference pane (FHS.prefPane).
# The version comes from the repo VERSION file.

VERSION := $(strip $(shell cat ../../VERSION 2>/dev/null || echo 0.0.0))

APP      := FHS
BUNDLE   := $(APP).app
CONTENTS := $(BUNDLE)/Contents
MACOS    := $(CONTENTS)/MacOS

PREFPANE := $(APP).prefPane
PPCONT   := $(PREFPANE)/Contents

UNINST   := Uninstall-FHS.app
UNCONT   := $(UNINST)/Contents

SWIFTFLAGS := -O
SIGNCERT   ?= -
ICONSET    := AppIcon.iconset

all: $(BUNDLE) $(PREFPANE) $(UNINST)

# Regenerate the icon PNGs from their source. Not part of `all`: the PNGs are
# committed build inputs, so a normal build does not need AppKit to draw them
# and the artwork cannot drift between machines.
icons: mkicons.swift
	swiftc -O -framework AppKit -o /tmp/fhs-mkicons mkicons.swift
	/tmp/fhs-mkicons .
	rm -f /tmp/fhs-mkicons

# The menu-bar app. LSUIElement in the plist keeps it out of the Dock.
$(BUNDLE): FHSState.swift FHSMenu.swift main.swift Info.plist.in ../../VERSION \
           appicon.png icon_menu.png
	mkdir -p $(MACOS) $(CONTENTS)/Resources
	swiftc $(SWIFTFLAGS) -framework Cocoa -framework ServiceManagement \
	    -o $(MACOS)/$(APP) FHSState.swift FHSMenu.swift main.swift
	@# Finder and Dock icon: render the standard iconset sizes from appicon.png
	@# and pack them into AppIcon.icns, named by CFBundleIconFile.
	rm -rf $(ICONSET)
	mkdir -p $(ICONSET)
	@for spec in 16:16x16 32:16x16@2x 32:32x32 64:32x32@2x 128:128x128 \
	             256:128x128@2x 256:256x256 512:256x256@2x 512:512x512 1024:512x512@2x; do \
	    px=$${spec%%:*}; nm=$${spec#*:}; \
	    sips -z $$px $$px appicon.png --out $(ICONSET)/icon_$$nm.png >/dev/null; \
	done
	iconutil -c icns $(ICONSET) -o $(CONTENTS)/Resources/AppIcon.icns
	rm -rf $(ICONSET)
	cp icon_menu.png $(CONTENTS)/Resources/
	sed -e 's/__FHSVERSION__/$(VERSION)/g' Info.plist.in > $(CONTENTS)/Info.plist
	xattr -cr $(BUNDLE)
	codesign -s "$(SIGNCERT)" --force --timestamp=none $(BUNDLE)

# The preference pane's principal binary is a loadable Mach-O bundle, loaded by
# System Settings - hence -emit-library -bundle rather than a normal executable.
# The uninstaller. Thin by design - it asks fhsctl to do the work, so the
# teardown sequence lives in exactly one place.
$(UNINST): Uninstall.swift uninstall-Info.plist.in ../../VERSION $(BUNDLE)
	mkdir -p $(UNCONT)/MacOS $(UNCONT)/Resources
	swiftc $(SWIFTFLAGS) -framework AppKit \
	    -o $(UNCONT)/MacOS/Uninstall-FHS Uninstall.swift
	@# Same icon as the app it removes, so the pair reads as one thing.
	cp $(CONTENTS)/Resources/AppIcon.icns $(UNCONT)/Resources/
	sed -e 's/__FHSVERSION__/$(VERSION)/g' uninstall-Info.plist.in > $(UNCONT)/Info.plist
	xattr -cr $(UNINST)
	codesign -s "$(SIGNCERT)" --force --timestamp=none $(UNINST)

$(PREFPANE): FHSState.swift FHSPref.swift prefpane-Info.plist.in ../../VERSION \
             appicon.png
	mkdir -p $(PPCONT)/MacOS $(PPCONT)/Resources
	swiftc $(SWIFTFLAGS) -emit-library -Xlinker -bundle \
	    -framework AppKit -framework PreferencePanes -framework ServiceManagement \
	    -module-name FHSPref \
	    -o $(PPCONT)/MacOS/$(APP) FHSState.swift FHSPref.swift
	cp appicon.png $(PPCONT)/Resources/
	sed -e 's/__FHSVERSION__/$(VERSION)/g' prefpane-Info.plist.in > $(PPCONT)/Info.plist
	xattr -cr $(PREFPANE)
	codesign -s "$(SIGNCERT)" --force --timestamp=none $(PREFPANE)

clean:
	rm -rf $(BUNDLE) $(PREFPANE) $(UNINST) $(ICONSET)

.PHONY: all icons clean
