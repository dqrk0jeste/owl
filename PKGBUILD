# Maintainer: Darko Nikolic <darkonikoloc@gmail.com>

pkgname=owl
pkgver=1.0
epoch=1
pkgrel=1
pkgdesc='tiling wayland compositor based on wlroots'
arch=(x86_64)
url='https://github.com/dqrk0jeste/owl'
license=(MIT)
depends=(
  'libinput'
  'libwayland-server.so'
  'libdrm'
  'pixman'
  'libxkbcommon.so'
  'wlroots'
)
makedepends=(make wayland-protocols)
provides=('wayland-compositor')
# backup=(
#   etc/sway/config
#   etc/sway/config.d/50-systemd-user.conf
# )
optdepends=(
  'kitty: terminal emulator used in the default configuration'
  'rofi-wayland: app-launcher used in the default configuration'
  'xdg-desktop-portal: base xdg-desktop-portal'
  'xdg-desktop-portal-gtk: default xdg-desktop-portal for file picking'
  'xdg-desktop-portal-wlr: xdg-desktop-portal backend'
)
source=("https://github.com/swaywm/sway/releases/download/$pkgver/sway-$pkgver.tar.gz"
        "https://github.com/swaywm/sway/releases/download/$pkgver/sway-$pkgver.tar.gz.sig")
install=sway.install
sha512sums=('f75a80506d2dcae722ea64c47fa423b9713bcfaa6541ffc353abd413238abb9ab7c88490d54e30ef09dc003215aa6a0005e5b425c9c943f982d5ff3c7cfad440'
            'SKIP'
            'd5f9aadbb4bbef067c31d4c8c14dad220eb6f3e559e9157e20e1e3d47faf2f77b9a15e52519c3ffc53dc8a5202cb28757b81a4b3b0cc5dd50a4ddc49e03fe06e'
            '4f9576b7218aef8152eb60e646985e96b13540b7a4fd34ba68fdc490199cf7a7b46bbee85587e41bffe81fc730222cf408d5712e6251edc85a0a0b0408c1a2df')
validpgpkeys=('34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48'  # Simon Ser
              '9DDA3B9FA5D58DD5392C78E652CB6609B22DA89A') # Drew DeVault

build() {
  cd "$pkgname-$pkgver"
  make
}

package() {
	install -Dm644 build/owl "$pkgdir/usr/bin/owl"
	install -Dm644 build/owl-ipc "$pkgdir/usr/bin/owl-ipc"
	install -Dm644 default.conf "$pkgdir/usr/share/owl/default.conf"
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
	install -Dm644 owl.desktop "$pkgdir/usr/share/wayland-sessions/owl.desktop;"
	install -Dm644 owl-portals.conf "$pkgdir/usr/share/xdg-desktop-portal/owl-portals.conf"
}

# vim: ts=2 sw=2 et
