# Builds the mSL/XNU menu-bar app (mSL.app) and preference pane (mSL.prefPane).
# The version comes from the repo VERSION file.

VERSION := $(strip $(shell cat ../../VERSION 2>/dev/null || echo 0.0.0))

APP      := mSL
BUNDLE   := $(APP).app
CONTENTS := $(BUNDLE)/Contents
MACOS    := $(CONTENTS)/MacOS

PREFPANE := $(APP).prefPane
PPCONT   := $(PREFPANE)/Contents

SWIFTFLAGS := -O
SIGNCERT   ?= -

all: $(BUNDLE) $(PREFPANE)

# The menu-bar app. LSUIElement in the plist keeps it out of the Dock.
$(BUNDLE): MSLState.swift MSLMenu.swift main.swift Info.plist.in ../../VERSION
	mkdir -p $(MACOS) $(CONTENTS)/Resources
	swiftc $(SWIFTFLAGS) -framework Cocoa -framework ServiceManagement \
	    -o $(MACOS)/$(APP) MSLState.swift MSLMenu.swift main.swift
	sed -e 's/__MSLVERSION__/$(VERSION)/g' Info.plist.in > $(CONTENTS)/Info.plist
	xattr -cr $(BUNDLE)
	codesign -s "$(SIGNCERT)" --force --timestamp=none $(BUNDLE)

# The preference pane's principal binary is a loadable Mach-O bundle, loaded by
# System Settings - hence -emit-library -bundle rather than a normal executable.
$(PREFPANE): MSLState.swift MSLPref.swift prefpane-Info.plist.in ../../VERSION
	mkdir -p $(PPCONT)/MacOS $(PPCONT)/Resources
	swiftc $(SWIFTFLAGS) -emit-library -Xlinker -bundle \
	    -framework AppKit -framework PreferencePanes -framework ServiceManagement \
	    -module-name MSLPref \
	    -o $(PPCONT)/MacOS/$(APP) MSLState.swift MSLPref.swift
	sed -e 's/__MSLVERSION__/$(VERSION)/g' prefpane-Info.plist.in > $(PPCONT)/Info.plist
	xattr -cr $(PREFPANE)
	codesign -s "$(SIGNCERT)" --force --timestamp=none $(PREFPANE)

clean:
	rm -rf $(BUNDLE) $(PREFPANE)

.PHONY: all clean
