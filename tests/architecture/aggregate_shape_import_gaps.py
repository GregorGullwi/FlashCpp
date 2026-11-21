"""Aggregate sampled canonical-shape import histograms over the migration corpus."""
from __future__ import annotations

import collections
import pathlib
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXE = ROOT / "x64/Sharded/FlashCppMSVC.exe"
BASELINE = ROOT / "tests/migration_counters/corpus_baseline.tsv"
OUT_MD = pathlib.Path(r"C:\Users\grego\AppData\Local\Temp\opencode\shape_import_gaps.md")

EXTRA_TESTS = [
	"tests/test_constexpr_inferred_multidim_array_flat_ret0.cpp",
	"tests/test_interleaved_pointer_array_subscript_identity_ret42.cpp",
	"tests/type_trait_ordered_pointer_ret42.cpp",
]

GROUP_BY_SITE = {
	"IrGenerator_Stmt_Decl": "array_extents",
	"IrGenerator_MemberAccess": "array_extents",
	"IrGenerator_Visitors_Decl": "array_extents",
	"ConstExprEvaluator_Materialize": "array_extents",
	"ConstExprEvalHelpers": "array_extents",
	"ConstExprEvaluator_tryGetConstexprTypeSizeBytes": "array_extents",
	"Parser_Statements": "array_extents",
	"Parser_Templates_Params": "array_extents",
	"SemanticAnalysis": "array_extents",
	"TypeTraitEvaluator": "pointer_presence_cv",
	"Parser_Expr_QualLookup_pointer_depth": "pointer_presence_cv",
	"TemplateExpressionEquivalence": "identity_equality",
	"ConstExprEvaluator_Bindings": "identity_equality",
	"Parser_Expr_QualLookup_pointer_levels": "identity_equality",
	"ConstExprEvaluator_MemberAccess": "derived_types",
	"Parser_Expr_PrimaryUnary": "derived_types",
	"IrGenerator_Expr_Conversions": "derived_types",
	"SemanticAnalysis_adoptPublishedStructuralType": "array_extents",
}


def corpus_paths() -> list[str]:
	paths = set()
	for raw in BASELINE.read_text(encoding="utf-8").splitlines():
		if not raw or raw.startswith("#"):
			continue
		paths.add(raw.split("\t")[0])
	return sorted(paths)


SHAPE_RE = re.compile(
	r"canonical-shape-import-shape site=(\S+) status=(\S+) category=(\S+) array=(\d+) dims=(\d+) "
	r"d0=(\d+) d1=(\d+) ptr=(\d+) ordered=(\d+) pointee_array=(\d+) ref=(\d+) entity=(\d+) layout=(\d+)"
)
REQUIRED_ARRAY_SITES = (
	"IrGenerator_MemberAccess",
	"IrGenerator_Visitors_Decl",
	"Parser_Statements",
	"ConstExprEvalHelpers",
	"ConstExprEvaluator_Materialize",
	"IrGenerator_Stmt_Decl",
)
NOMINAL_SITES = (
	"IrGenerator_Stmt_Decl",
	"IrGenerator_MemberAccess",
	"IrGenerator_Visitors_Decl",
	"Parser_Statements",
	"ConstExprEvalHelpers",
	"ConstExprEvaluator_Materialize",
	"SemanticAnalysis",
	"SemanticAnalysis_adoptPublishedStructuralType",
	"Parser_Expr_PrimaryUnary",
)
SKIP_SOURCES = {
	"linux_exception_stubs.cpp",
	"test_builtin_constant_p_ret42.cpp",
}


def suite_sources() -> list[pathlib.Path]:
	sources = []
	for source in sorted((ROOT / "tests").glob("*.cpp")):
		if source.name in SKIP_SOURCES:
			continue
		sources.append(source)
	return sources


def compile_and_scan(source: pathlib.Path) -> tuple[collections.Counter[tuple[str, str]], collections.Counter[tuple[str, ...]], int]:
	hist: collections.Counter[tuple[str, str]] = collections.Counter()
	shapes: collections.Counter[tuple[str, ...]] = collections.Counter()
	gap = 0
	obj = ROOT / "x64" / f"shape_gap_{abs(hash(source.as_posix()))}.obj"
	result = subprocess.run(
		[str(EXE), "--perf-stats", "--log-level=Types:debug", "-o", str(obj), str(source)],
		capture_output=True,
		text=True,
	)
	text = result.stdout + result.stderr
	for line in text.splitlines():
		match = re.search(r"canonical-shape-import-histogram site=(\S+) status=(\S+) count=(\d+)", line)
		if match:
			hist[(match.group(1), match.group(2))] += int(match.group(3))
		shape_match = SHAPE_RE.search(line)
		if shape_match:
			shapes[shape_match.groups()] += 1
			if shape_match.group(1) in ("ConstExprEvaluator_Materialize", "IrGenerator_Stmt_Decl") and shape_match.group(2) == "UnmigratedNominal":
				print(f"REMAINING {source.name} {shape_match.group(0)}", flush=True)
		gap_match = re.search(r"Canonical shape import gaps:\s*(\d+)", line)
		if gap_match:
			gap += int(gap_match.group(1))
	return hist, shapes, gap


def main() -> int:
	if not EXE.is_file():
		print(f"ERROR: missing compiler {EXE}", file=sys.stderr)
		return 1
	total_hist: collections.Counter[tuple[str, str]] = collections.Counter()
	total_shapes: collections.Counter[tuple[str, ...]] = collections.Counter()
	total_gaps = 0
	sources = suite_sources()
	completed = 0
	with ThreadPoolExecutor(max_workers=8) as pool:
		for partial, shapes, gap in pool.map(compile_and_scan, sources, chunksize=8):
			total_hist.update(partial)
			total_shapes.update(shapes)
			total_gaps += gap
			completed += 1
			if completed % 200 == 0:
				print(f"scanned {completed}/{len(sources)}", flush=True)
	by_group: dict[str, collections.Counter[str]] = collections.defaultdict(collections.Counter)
	for (site, status), count in total_hist.items():
		group = GROUP_BY_SITE.get(site, "other")
		by_group[group][status] += count
	lines = [
		"# Sampled canonical shape import gap histogram",
		"",
		f"Full-suite compilations: {len(sources)}",
		f"Total `canonical_shape_import_gap` counter increments: {total_gaps}",
		"",
		"## Per-site histogram (aggregated)",
		"",
		"| Site | Status | Count |",
		"|---|---|---:|",
	]
	for (site, status), count in sorted(total_hist.items()):
		lines.append(f"| {site} | {status} | {count} |")
	lines.extend(["", "## By capability group (non-Supported only)", ""])
	for group in sorted(by_group):
		non_supported = {k: v for k, v in by_group[group].items() if k != "Supported"}
		if not non_supported:
			lines.append(f"- **{group}**: all sampled imports `Supported`")
			continue
		top = max(non_supported.items(), key=lambda item: item[1])
		lines.append(f"- **{group}**: top blocking status `{top[0]}` ({top[1]} hits)")
	lines.extend(["", "## Array-extent sites", ""])
	for site in REQUIRED_ARRAY_SITES:
		site_rows = [(status, count) for (row_site, status), count in sorted(total_hist.items()) if row_site == site]
		if not site_rows:
			lines.append(f"- **{site}**: 0 hits")
			continue
		detail = ", ".join(f"{status}={count}" for status, count in site_rows)
		lines.append(f"- **{site}**: {detail}")
	lines.extend(["", "## UnmigratedNominal specifier shapes", ""])
	for site in NOMINAL_SITES:
		rows = [(shape, count) for shape, count in total_shapes.items() if shape[0] == site and shape[1] == "UnmigratedNominal"]
		if not rows:
			lines.append(f"- **{site}**: no UnmigratedNominal shapes")
			continue
		lines.append(f"- **{site}**:")
		for shape, count in sorted(rows, key=lambda item: (-item[1], item[0])):
			_, _, category, array, dims, d0, d1, ptr, ordered, pointee_array, ref, entity, layout = shape
			lines.append(
				f"  - category={category} array={array} dims={dims} d0={d0} d1={d1} ptr={ptr} "
				f"ordered={ordered} pointee_array={pointee_array} ref={ref} entity={entity} layout={layout} count={count}"
			)
	OUT_MD.parent.mkdir(parents=True, exist_ok=True)
	OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
	print(f"Wrote {OUT_MD}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())