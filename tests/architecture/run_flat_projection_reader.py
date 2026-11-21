"""Static inventory of flat pointer/array projection reads under src/.

Counts TypeSpecifierNode flat accessors, alias readers, and CanonicalTypeDesc
flat field reads. Per-file baseline may only ratchet downward.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src"
BASELINE = ROOT / "tests/migration_counters/flat_projection_reader_baseline.tsv"

ALLOWLIST_FILES = frozenset({
	"src/CanonicalTypeAdapter.h",
	"src/AstNodeTypes_DeclNodes.h",
})

METHOD_PATTERNS = [
	re.compile(r"\bpointer_levels\s*\(\s*\)"),
	re.compile(r"\barray_dimensions\s*\(\s*\)"),
	re.compile(r"\bis_pointer\s*\(\s*\)"),
	re.compile(r"\bpointer_depth\s*\(\s*\)"),
	re.compile(r"\barray_size\s*\(\s*\)"),
	re.compile(r"\barray_dimension_count\s*\(\s*\)"),
	re.compile(r"\blegacy_declarator_pointer_wrappers\s*\(\s*\)"),
	re.compile(r"\bwithout_innermost_legacy_pointer_wrapper\s*\(\s*\)"),
]

DESC_FIELD_PATTERNS = [
	re.compile(r"\.pointer_levels\b(?!\s*\()"),
	re.compile(r"\.array_dimensions\b(?!\s*\()"),
]

# DeclarationNode::array_dimensions() holds bound expressions, not the projection.
# Exclude when the immediate receiver name matches declaration-side naming.
DECL_ARRAY_DIMENSION_RECEIVERS = re.compile(
	r"(?<![.\w])"
	r"(?:decl(?:_node|aration)?|var_decl|qualified_var|member_decl|param_decl|"
	r"global_decl|field_decl|binding_decl|enumerator_decl|catch_decl)"
	r"\s*\.\s*array_dimensions\s*\(",
)

TYPE_SPEC_RECEIVERS = re.compile(
	r"(?<![.\w])"
	r"(?:type(?:_node|_spec(?:ifier)?|_specifier_node)?|element_type|operand_type|"
	r"member_type|lhs|rhs|first|second|syntax|spec|materialized|projected|"
	r"normalized_lhs|normalized_rhs|target_type|base_type|array_type|param_type|"
	r"declared_type|type_spec|type1|type2|current|candidate|bound_spec|"
	r"element_desc|destination_desc|source_desc|conversion_return_type)"
	r"\s*\.\s*array_dimensions\s*\(",
)

# Named spelling / diagnostic sites (function-local allowlists).
FUNCTION_ALLOWLIST_SPANS: dict[str, list[tuple[int, int]]] = {
	"src/Parser_Expr_QualLookup.cpp": [(5389, 5600)],  # Parser::type_to_string
	"src/AstNodeTypes.cpp": [(2133, 2205)],  # readableFunctionDeclarator spelling
	"src/IrGenerator_Visitors_Decl.cpp": [(390, 410)],  # debug FLASH_LOG loop
}


def strip_c_comments(text: str) -> str:
	out: list[str] = []
	index = 0
	length = len(text)
	while index < length:
		if text[index:index + 2] == "//":
			while index < length and text[index] != "\n":
				index += 1
			continue
		if text[index:index + 2] == "/*":
			index += 2
			while index < length and text[index:index + 2] != "*/":
				index += 1
			index = min(index + 2, length)
			continue
		out.append(text[index])
		index += 1
	return "".join(out)


def is_template_type_arg_field_init(line: str) -> bool:
	stripped = line.strip()
	if "TemplateTypeArg" not in stripped and "template type argument" not in stripped.lower():
		return False
	return bool(re.search(r"\b(?:pointer_depth|array_dimensions|pointer_levels)\b", stripped)) and (
		";" in stripped or "=" in stripped
	) and not re.search(r"\(\s*\)", stripped)


def is_struct_field_declaration(line: str) -> bool:
	stripped = line.strip()
	if re.search(r"\b(?:pointer_levels|array_dimensions)\s*\(\s*\)", stripped):
		return False
	return bool(re.search(r"\b(?:pointer_levels|array_dimensions)\s*;", stripped))


def line_in_allowlist_span(rel_path: str, line_no: int) -> bool:
	for start, end in FUNCTION_ALLOWLIST_SPANS.get(rel_path, ()):
		if start <= line_no <= end:
			return True
	return False


def count_projection_reads(rel_path: str, content: str) -> int:
	if rel_path in ALLOWLIST_FILES:
		return 0
	stripped_content = strip_c_comments(content)
	lines = stripped_content.splitlines()
	total = 0
	for line_no, line in enumerate(lines, start=1):
		if line_in_allowlist_span(rel_path, line_no):
			continue
		if is_template_type_arg_field_init(line) or is_struct_field_declaration(line):
			continue
		if "array_dimensions()" in line:
			if DECL_ARRAY_DIMENSION_RECEIVERS.search(line):
				continue
			if not TYPE_SPEC_RECEIVERS.search(line) and re.search(
				r"\.\s*array_dimensions\s*\(", line
			):
				receiver = line.split("array_dimensions", 1)[0].rstrip()
				if re.search(r"\bdecl", receiver, re.IGNORECASE):
					continue
		for pattern in METHOD_PATTERNS:
			total += len(pattern.findall(line))
		for pattern in DESC_FIELD_PATTERNS:
			total += len(pattern.findall(line))
	return total


def scan_src() -> dict[str, int]:
	counts: dict[str, int] = {}
	for path in sorted(SRC.rglob("*")):
		if path.suffix not in {".cpp", ".h", ".hpp"}:
			continue
		rel = path.relative_to(ROOT).as_posix()
		counts[rel] = count_projection_reads(rel, path.read_text(encoding="utf-8", errors="replace"))
	return counts


def load_baseline() -> dict[str, int]:
	if not BASELINE.is_file():
		return {}
	baseline: dict[str, int] = {}
	for raw in BASELINE.read_text(encoding="utf-8").splitlines():
		line = raw.strip()
		if not line or line.startswith("#"):
			continue
		parts = line.split("\t")
		if len(parts) != 2:
			raise SystemExit(f"Malformed baseline line: {raw}")
		baseline[parts[0]] = int(parts[1])
	return baseline


def write_baseline(counts: dict[str, int]) -> None:
	rows = [f"{path}\t{count}" for path, count in sorted(counts.items()) if count > 0]
	BASELINE.parent.mkdir(parents=True, exist_ok=True)
	BASELINE.write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--update-baseline", action="store_true")
	options = parser.parse_args()
	counts = scan_src()
	nonzero = {path: count for path, count in counts.items() if count > 0}
	total = sum(nonzero.values())
	print(f"Flat projection reader inventory: total={total} files_with_reads={len(nonzero)}")
	if options.update_baseline:
		write_baseline(counts)
		print(f"Baseline updated: {BASELINE}")
		return 0
	baseline = load_baseline()
	if not baseline:
		print(f"ERROR: missing baseline {BASELINE}", file=sys.stderr)
		return 1
	failed = False
	improved: list[str] = []
	for path, base_count in sorted(baseline.items()):
		actual = counts.get(path, 0)
		if actual > base_count:
			print(f"REGRESSION {path}: actual={actual} baseline={base_count}", file=sys.stderr)
			failed = True
		elif actual < base_count:
			improved.append(f"{path}: {base_count} -> {actual}")
	for path, actual in sorted(nonzero.items()):
		if path not in baseline:
			print(f"UNTRACKED {path}: actual={actual} (add to baseline)", file=sys.stderr)
			failed = True
	if improved:
		print("Hint: flat-projection counts decreased; run with --update-baseline after review:")
		for hint in improved:
			print(f"  {hint}")
	if failed:
		return 1
	print("RESULT: OK - flat projection reader inventory within per-file baselines")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())