{
  wayland-protocols,
  wayland-scanner,
  libxkbcommon,
  pkg-config,
  libinput,
  wlroots_git ? (builtins.getFlake "github:chaotic-cx/nyx").packages.${builtins.currentSystem}.wlroots_git,
  wayland,
  pixman,
  libxcb,
  libdrm,
  sudo,
  
  fetchgit,
  stdenv,
  pkgs ? import <nixpkgs>,
  lib,
}:

stdenv.mkDerivation rec {
  pname = "owl";
  version = "_git"; # no actual version yet lmao

  src = fetchgit {
    url = "https://github.com/dqrk0jeste/owl";
    hash = "sha256-8v0gkobA/+eXoXQaKU02VnWXHg3ipvoH3saCRTvM1Fw=";
  };

  nativeBuildInputs = [
    pkg-config
    wayland-scanner
    gnumake
  ];

  buildInputs = [
    libinput
    libxcb
    libdrm
    libxkbcommon
    pixman
    wayland
    wayland-protocols
    wlroots_git
  ];

  src = ./.;

  buildPhase = ''
    make
  '';

  installPhase = ''
    mkdir -p $out/bin
    mkdir -p $out/share
    cp build/owl $out/bin/
    cp build/owl-ipc $out/bin/
    cp default.conf $out/share/
  '';

  strictDeps = true;

  depsBuildBuild = [
    pkg-config
    sudo
  ];

  __structuredAttrs = true;

  meta = {
    description = "tiling wayland compositor based on wlroots.";
    homepage = "https://github.com/dqrk0jeste/owl";
    license = lib.licenses.mit;
    maintainers = with lib.maintainers; [ s0me1newithhand7s ];
    platforms = lib.platforms.all;
  };
}
