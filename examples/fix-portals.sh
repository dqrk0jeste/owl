#!/usr/bin/env bash

sleep 2
killall xdg-desktop-portal
/usr/lib/xdg-desktop-portal-wlr &
sleep 2
/usr/lib/xdg-desktop-portal &
