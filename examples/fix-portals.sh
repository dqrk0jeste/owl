#!/usr/bin/env bash

sleep 1
systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP
dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_CURRENT_DESKTOP=owl

sleep 1
killall xdg-desktop-portal

sleep 1
/usr/lib/xdg-desktop-portal-wlr &

sleep 1
/usr/lib/xdg-desktop-portal &
