PKG_CONFIG?=pkg-config
WAYLAND_PROTOCOLS!=$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols
WAYLAND_SCANNER!=$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner

PKGS="wlroots-0.19" wayland-server xkbcommon libinput
CFLAGS_PKG_CONFIG!=$(PKG_CONFIG) --cflags $(PKGS)
CFLAGS+=$(CFLAGS_PKG_CONFIG)
CFLAGS+=-Ibuild/protocols
LIBS!=$(PKG_CONFIG) --libs $(PKGS)

all: build/owl build/default.conf

build:
	mkdir -p build

build/protocols: build
	mkdir -p build/protocols

build/protocols/xdg-shell-protocol.h: build/protocols
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

build/protocols/wlr-layer-shell-unstable-v1-protocol.h: build/protocols
	$(WAYLAND_SCANNER) server-header \
		./protocols/wlr-layer-shell-unstable-v1.xml $@

build/protocols/xdg-output-unstable-v1-protocol.h: build/protocols
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/unstable/xdg-output/xdg-output-unstable-v1.xml $@

build/owl.o: owl.c build/protocols/xdg-shell-protocol.h build/protocols/wlr-layer-shell-unstable-v1-protocol.h build/protocols/xdg-output-unstable-v1-protocol.h
	$(CC) -c $< $(CFLAGS) -I. -DWLR_USE_UNSTABLE -o $@

build/owl: build/owl.o
	$(CC) $^ $> $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@

build/default.conf: build default.conf
	cp default.conf build/default.conf

install: build/owl build/default.conf
	sudo cp build/owl /usr/bin/owl && \
	sudo mkdir -p /usr/share/owl && \
	sudo cp build/default.conf /usr/share/owl/default.conf

uninstall:
	sudo rm /usr/bin/owl 2>/dev/null && \
	sudo rm -rf /usr/share/owl 2>/dev/null 

clean:
	rm -rf build 2>/dev/null

.PHONY: all clean install uninstall
