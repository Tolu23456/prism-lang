{pkgs}: {
  deps = [
    pkgs.gnumake
    pkgs.gcc
    pkgs.dejavu_fonts
    pkgs.pkg-config
    pkgs.fontconfig
    pkgs.freetype
    pkgs.xorg.libXrender
    pkgs.xorg.libXft
    pkgs.xorg.libX11
    pkgs.gir-rs
    pkgs.unzip
  ];
}
