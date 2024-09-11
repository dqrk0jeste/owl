PKG_CONFIG?=pkg-config
WAYLAND_PROTOCOLS!=$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols
WAYLAND_SCANNER!=$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner

PKGS="wlroots-0.19" wayland-server xkbcommon
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)
CFLAGS+=$(CFLAGS_PKG_CONFIG)
CFLAGS+=-Iprotocols
LIBS!=$(PKG_CONFIG) --libs $(PKGS)

all: build/owl

protocols/xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

protocols/xdg-decoration-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml $@

build/owl.o: owl.c protocols/xdg-shell-protocol.h protocols/xdg-decoration-protocol.h
	$(CC) -c $< $(CFLAGS) -I. -DWLR_USE_UNSTABLE -o $@

build/owl: build/owl.o
	$(CC) $^ $> $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f protocols/* build/*

.PHONY: all clean
