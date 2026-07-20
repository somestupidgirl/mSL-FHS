#!/bin/bash
#
# Uninstall mSL/FHS.
#
# Order matters: every component is switched off *before* anything is removed.
# Disabling is what restores /etc/auto_master and drops the /etc/synthetic.conf
# entries, and fhsctl is the thing that knows how to do it - deleting the tool
# first would strand the system with a masked automounter line and no supported
# way to put it back.
#
set -u

echo "This will remove mSL/FHS and turn off every component."
printf "Continue? [y/N] "
read -r reply
case "$reply" in
    [yY]*) ;;
    *) echo "Cancelled."; exit 0 ;;
esac

echo "==> Authorizing"
sudo -v || { echo "Cancelled."; exit 1; }

echo "==> Stopping the daemon"
sudo launchctl bootout system/com.beako.fhsxd 2>/dev/null || true

if [ -x /usr/local/sbin/fhsctl ]; then
    echo "==> Disabling components"
    sudo /usr/local/sbin/fhsctl media disable || true
    sudo /usr/local/sbin/fhsctl mnt disable   || true
    sudo /usr/local/sbin/fhsctl home disable  || true
else
    echo "==> fhsctl is missing; cannot disable components automatically."
    echo "    If /etc/auto_master still contains a line beginning"
    echo "    '#fhs:disabled#' or '#msl:disabled#', restore it from"
    echo "    /var/db/fhs.auto_master.orig."
fi

echo "==> Quitting the menu-bar app"
osascript -e 'tell application "mSL" to quit' 2>/dev/null || true
killall mSL 2>/dev/null || true

echo "==> Removing files"
sudo rm -f  /Library/LaunchDaemons/com.beako.fhsxd.plist
sudo rm -f  /usr/local/sbin/fhsctl /usr/local/sbin/fhsxd
sudo rm -rf /Applications/mSL/FHS.app
sudo rm -rf /Library/PreferencePanes/FHS.prefPane
sudo rm -f  /var/db/fhs.home /var/db/fhs.mnt /var/db/fhs.media
sudo pkgutil --forget com.beako.fhs.pkg 2>/dev/null || true

echo
echo "mSL/FHS has been removed."
echo "A backup of your original /etc/auto_master is kept at"
echo "  /var/db/fhs.auto_master.orig"
echo "Any /mnt and /media directories remain until the next restart."
