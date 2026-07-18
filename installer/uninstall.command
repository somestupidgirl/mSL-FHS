#!/bin/bash
#
# Uninstall mSL/XNU.
#
# Order matters: every component is switched off *before* anything is removed.
# Disabling is what restores /etc/auto_master and drops the /etc/synthetic.conf
# entries, and mslctl is the thing that knows how to do it - deleting the tool
# first would strand the system with a masked automounter line and no supported
# way to put it back.
#
set -u

echo "This will remove mSL/XNU and turn off every component."
printf "Continue? [y/N] "
read -r reply
case "$reply" in
    [yY]*) ;;
    *) echo "Cancelled."; exit 0 ;;
esac

echo "==> Authorizing"
sudo -v || { echo "Cancelled."; exit 1; }

echo "==> Stopping the daemon"
sudo launchctl bootout system/com.beako.mslxd 2>/dev/null || true

if [ -x /usr/local/sbin/mslctl ]; then
    echo "==> Disabling components"
    sudo /usr/local/sbin/mslctl media disable || true
    sudo /usr/local/sbin/mslctl mnt disable   || true
    sudo /usr/local/sbin/mslctl home disable  || true
else
    echo "==> mslctl is missing; cannot disable components automatically."
    echo "    If /etc/auto_master still contains a line beginning"
    echo "    '#msl:disabled#', restore it from /var/db/msl.auto_master.orig."
fi

echo "==> Quitting the menu-bar app"
osascript -e 'tell application "mSL" to quit' 2>/dev/null || true
killall mSL 2>/dev/null || true

echo "==> Removing files"
sudo rm -f  /Library/LaunchDaemons/com.beako.mslxd.plist
sudo rm -f  /usr/local/sbin/mslctl /usr/local/sbin/mslxd
sudo rm -rf /Applications/mSL.app
sudo rm -rf /Library/PreferencePanes/mSL.prefPane
sudo rm -f  /var/db/msl.home /var/db/msl.mnt /var/db/msl.media
sudo pkgutil --forget com.beako.msl.pkg 2>/dev/null || true

echo
echo "mSL/XNU has been removed."
echo "A backup of your original /etc/auto_master is kept at"
echo "  /var/db/msl.auto_master.orig"
echo "Any /mnt and /media directories remain until the next restart."
