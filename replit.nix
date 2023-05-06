{ pkgs }: {
	deps = [
  pkgs.lld_12
  pkgs.nasm
  pkgs.clang_12
		pkgs.ccls
		pkgs.gdb
		pkgs.gnumake
	];
}