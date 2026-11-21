"""Measure canonical_shape_import_gap per migration corpus path and update baseline rows only."""
from __future__ import annotations

import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXE = ROOT / "x64/Sharded/FlashCppMSVC.exe"
BASELINE = ROOT / "tests/migration_counters/corpus_baseline.tsv"


def measure_gap(source: pathlib.Path) -> int:
	obj = ROOT / "x64" / f"gap_measure_{abs(hash(source.as_posix()))}.obj"
	result = subprocess.run(
		[str(EXE), "--perf-stats", "-o", str(obj), str(source)],
		capture_output=True,
		text=True,
	)
	text = result.stdout + result.stderr
	match = re.search(r"Canonical shape import gaps:\s*(\d+)", text)
	if not match:
		raise RuntimeError(f"missing gap counter for {source}")
	return int(match.group(1))


def main() -> None:
	paths = sorted({
		line.split("\t")[0]
		for line in BASELINE.read_text(encoding="utf-8").splitlines()
		if line and not line.startswith("#")
	})
	measured = {}
	for rel in paths:
		source = ROOT / rel
		if source.is_file():
			measured[rel] = measure_gap(source)
	out_lines = []
	for raw in BASELINE.read_text(encoding="utf-8").splitlines():
		if not raw or raw.startswith("#"):
			out_lines.append(raw)
			continue
		path, counter, count = raw.split("\t")
		if counter == "canonical_shape_import_gap" and path in measured:
			out_lines.append(f"{path}\t{counter}\t{measured[path]}")
		else:
			out_lines.append(raw)
	BASELINE.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
	for path, gap in sorted(measured.items()):
		print(f"{path}\tcanonical_shape_import_gap\t{gap}")


if __name__ == "__main__":
	main()