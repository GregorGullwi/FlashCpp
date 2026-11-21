#!/usr/bin/env bash
# Static flat-projection reader inventory under src/.
#
# Per-file counts may only ratchet downward.
#
# Usage:
#   bash tests/run_flat_projection_reader.sh
#   bash tests/run_flat_projection_reader.sh --update-baseline

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"

python_bin=""
if command -v python3 >/dev/null 2>&1; then
	python_bin=python3
elif command -v python >/dev/null 2>&1; then
	python_bin=python
else
	echo "ERROR: python not found" >&2
	exit 1
fi

args=(tests/architecture/run_flat_projection_reader.py)
if [[ "${1:-}" == "--update-baseline" ]]; then
	args+=(--update-baseline)
fi
exec "$python_bin" "${args[@]}"