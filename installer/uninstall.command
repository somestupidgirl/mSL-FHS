#!/bin/bash
#
# Uninstall mSL/FHS from the disk image.
#
# A thin wrapper, on purpose. The teardown lives in fhsctl, which is the only
# place that knows the full component list; a copy of the sequence here had
# already drifted, switching off three of the seven components and leaving the
# rest with live /etc/synthetic.conf entries after an "uninstall".
#
set -u

FHSCTL=/usr/local/sbin/fhsctl

if [ ! -x "$FHSCTL" ]; then
    echo "mSL/FHS does not appear to be installed: $FHSCTL is missing."
    echo
    echo "If a partial install left files behind, your original /etc/auto_master"
    echo "is kept at /var/db/fhs.auto_master.orig. A line beginning"
    echo "'#fhs:disabled#' or '#msl:disabled#' there is one this layer masked."
    exit 1
fi

echo "This will switch every mSL/FHS component off and remove all installed files."
printf "Continue? [y/N] "
read -r reply
case "$reply" in
    [yY]*) ;;
    *) echo "Cancelled."; exit 0 ;;
esac

echo "==> Authorizing"
sudo -v || { echo "Cancelled."; exit 1; }

osascript -e 'tell application "FHS" to quit' 2>/dev/null || true
killall FHS 2>/dev/null || true

sudo "$FHSCTL" uninstall
