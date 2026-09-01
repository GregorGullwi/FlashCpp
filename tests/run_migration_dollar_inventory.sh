#!/usr/bin/env bash
# Static inventory guard for inline find('$') template-hash recovery sites.
#
# Counts may only ratchet downward. Removal boundary: architecture boundary 3B.
#
# Usage:
#   bash tests/run_migration_dollar_inventory.sh
#   bash tests/run_migration_dollar_inventory.sh --update-baseline

set -euo pipefail

update_baseline=0
if [ "${1:-}" = '--update-baseline' ]; then
	update_baseline=1
elif [ "$#" -ne 0 ]; then
	printf 'Usage: %s [--update-baseline]\n' "$0" >&2
	exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
# shellcheck source=runner/runner_common.sh
. "$script_dir/runner/runner_common.sh"

baseline_path="$script_dir/migration_counters/dollar_find_baseline.txt"
actual=$(runner_dollar_find_inventory_count "$repo_root")

if [ "$update_baseline" -eq 1 ]; then
	printf '%s\n' "$actual" > "$baseline_path"
	printf 'Dollar find('\''$'\'') baseline updated to %s\n' "$actual"
	exit 0
fi

if [ ! -f "$baseline_path" ]; then
	printf 'ERROR: Baseline file not found: %s\n' "$baseline_path" >&2
	exit 1
fi

baseline=$(head -1 "$baseline_path" | tr -d '[:space:]')
if ! [[ "$baseline" =~ ^[0-9]+$ ]]; then
	printf 'ERROR: Malformed dollar inventory baseline: %s\n' "$baseline" >&2
	exit 1
fi

printf "Dollar find('$') inventory: actual=%s baseline=%s\n" "$actual" "$baseline"
if [ "$actual" -gt "$baseline" ]; then
	printf 'RESULT: FAILED - inline dollar recovery inventory increased\n' >&2
	exit 1
fi
printf 'RESULT: OK - dollar inventory within baseline\n'
