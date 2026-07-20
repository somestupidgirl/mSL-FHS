mSL/FHS - macOS Subsystem for Linux / X is Now UNIX
===================================================

A filesystem-layout compatibility layer that presents macOS through a
Linux-shaped namespace: /home, /mnt and /media, backed by the real macOS
filesystem. No container, no virtual machine, no kernel extension.

INSTALL
    Open the .pkg and follow the installer.

    Nothing is enabled by the install. Open System Settings -> mSL/FHS, or
    the menu-bar icon, and switch on what you want.

    /home applies immediately. /mnt and /media appear after a restart -
    macOS creates root-level directories only at startup.

UNINSTALL
    Open /Applications/mSL/Uninstall-FHS.app, installed alongside the app.
    Or run "Uninstall FHS.command" from this disk image, or
    "sudo fhsctl uninstall" from a terminal - all three do the same thing.

    Every component is switched off first. That is what restores
    /etc/auto_master and withdraws the /etc/synthetic.conf entries, so the
    system is left as it was found.

WHAT GETS INSTALLED
    /usr/local/sbin/fhsctl                       command-line control
    /usr/local/sbin/fhsxd                        layout daemon
    /Library/LaunchDaemons/com.beako.fhsxd.plist daemon launchd job
    /Applications/mSL/FHS.app                    menu-bar app
    /Applications/mSL/Uninstall-FHS.app          uninstaller
    /Library/PreferencePanes/FHS.prefPane        System Settings pane

WHAT IT CHANGES, ONLY WHEN YOU ENABLE A COMPONENT
    /home    comments out the auto_home line in /etc/auto_master (a pristine
             copy is kept at /var/db/fhs.auto_master.orig) and creates
             symlinks to /Users
    /mnt     adds an /etc/synthetic.conf entry; the directory stays empty,
             which is what /mnt is on Linux
    /media   adds an /etc/synthetic.conf entry and maintains symlinks to
             removable volumes mounted in /Volumes

REQUIREMENTS
    macOS 15 or later, Apple Silicon or Intel. No kernel extension, no
    Reduced Security, no SIP changes.

The pseudo-filesystems that supply /proc and /sys are separate projects with
their own installers. mSL/FHS reports their state but never manages them.
