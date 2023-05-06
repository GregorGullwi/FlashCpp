{ pkgs }: {
	deps = [
		pkgs.nasm
  pkgs.clang_12
		pkgs.ccls
		pkgs.gdb
		pkgs.gnumake
	];
}