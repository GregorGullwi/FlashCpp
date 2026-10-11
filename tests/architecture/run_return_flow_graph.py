"""Build and run the native IR control-flow graph regression."""

import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/architecture/return_flow_graph_ret0.cpp"
OUTPUT = ROOT / "x64/return-flow-graph"


def main():
	OUTPUT.mkdir(parents=True, exist_ok=True)
	if sys.platform == "win32":
		obj = OUTPUT / "return_flow_graph_ret0.obj"
		executable = OUTPUT / "return_flow_graph_ret0.exe"
		compile_command = ["clang-cl", "/nologo", "/std:c++20", "/EHsc", "/W4", "/WX",
			"/I" + str(ROOT / "src"), "/c", str(SOURCE), "/Fo" + str(obj), "/clang:-fstack-usage"]
		link_command = ["clang-cl", "/nologo", str(obj), "/Fe" + str(executable)]
	else:
		obj = OUTPUT / "return_flow_graph_ret0.o"
		executable = OUTPUT / "return_flow_graph_ret0"
		compile_command = ["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
			"-I" + str(ROOT / "src"), "-c", str(SOURCE), "-fstack-usage", "-o", str(obj)]
		link_command = ["clang++", str(obj), "-o", str(executable)]
	subprocess.run(compile_command, cwd=OUTPUT, check=True)
	subprocess.run(link_command, cwd=OUTPUT, check=True)
	subprocess.run([str(executable)], cwd=ROOT, check=True)
	usage_files = list(OUTPUT.glob("*.su"))
	if usage_files:
		frames = []
		for usage_file in usage_files:
			for line in usage_file.read_text(encoding="utf-8").splitlines():
				parts = line.rsplit("\t", 2)
				if len(parts) == 3:
					try:
						frames.append((int(parts[1]), parts[0]))
					except ValueError:
						pass
		if frames:
			print("Largest reported stack frame: %d bytes (%s)" % max(frames))
			graph_frames = [frame for frame in frames if "IrControlFlowGraph" in frame[1]]
			if graph_frames:
				print("Largest graph analysis frame: %d bytes (%s)" % max(graph_frames))
	print("Return-flow graph regression passed")


if __name__ == "__main__":
	main()
