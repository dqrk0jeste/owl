#!/usr/bin/env bash

systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP
dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP=owl
sleep 2

killall xdg-desktop-portal
/usr/lib/xdg-desktop-portal-wlr &

sleep 2
/usr/lib/xdg-desktop-portal &
