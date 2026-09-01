#!/usr/bin/env bash
# Migration counter baseline check for FlashCpp (bash)
#
# Enforces directional compatibility counters over a fixed corpus
# (front-end rearchitecture, architecture boundaries 0 and 4). Counts may
# only ratchet downward; an increase fails the run.
#
# Usage:
#   bash tests/run_migration_counters.sh
#   bash tests/run_migration_counters.sh --update-baseline

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

declare -A RUNNER_MIGRATION_COUNTER_VALUES=()

baseline_path="$script_dir/migration_counters/corpus_baseline.tsv"
temp_dir=$(mktemp -d /tmp/flashcpp_counters_XXXXXX)
trap 'rm -rf "$temp_dir"' EXIT

cd "$repo_root"

printf '==============================================\n'
printf 'FlashCpp Migration Counter Check\n'
printf '==============================================\n\n'

if ! runner_resolve_flashcpp_compiler_path "$repo_root"; then
	printf 'ERROR: FlashCpp compiler not found under x64/. Run make sharded or .\\build_flashcpp.bat first.\n' >&2
	exit 1
fi
flashcpp_bin="$RUNNER_FLASHCPP_COMPILER_PATH"

if ! runner_binary_is_fresh "$flashcpp_bin" "$repo_root"; then
	printf 'ERROR: Compiler binary is older than %s. Rebuild the compiler and retry.\n' "$RUNNER_NEWEST_SOURCE" >&2
	exit 1
fi

if [ ! -f "$baseline_path" ]; then
	printf 'ERROR: Baseline file not found: %s\n' "$baseline_path" >&2
	exit 1
fi

declare -a entry_paths=()
declare -a entry_counters=()
declare -a entry_baselines=()
declare -A unique_paths=()
while IFS=$'\t' read -r path counter baseline || [ -n "${path:-}" ]; do
	[ -z "${path:-}" ] && continue
	[[ "$path" == \#* ]] && continue
	if ! [[ "$baseline" =~ ^[0-9]+$ ]]; then
		printf 'ERROR: Malformed baseline line (expected path, counter, count): %s\t%s\t%s\n' "$path" "$counter" "$baseline" >&2
		exit 1
	fi
	entry_paths+=("$path")
	entry_counters+=("$counter")
	entry_baselines+=("$baseline")
	unique_paths["$path"]=1
done < "$baseline_path"

if [ "${#entry_paths[@]}" -eq 0 ]; then
	printf 'ERROR: Baseline file contains no corpus entries.\n' >&2
	exit 1
fi

declare -A compiled_values=()
declare -a unmeasurable=()
declare -a regressions=()
declare -a improvements=()
index=0

for path in $(printf '%s\n' "${!unique_paths[@]}" | sort); do
	source_path="$repo_root/$path"
	if [ ! -f "$source_path" ]; then
		unmeasurable+=("$path (missing source file)")
		continue
	fi
	index=$((index + 1))
	obj_path="$temp_dir/corpus_$index.obj"
	output=$("$flashcpp_bin" --perf-stats -o "$obj_path" "$source_path" 2>&1 || true)
	if ! runner_parse_migration_counter_values "$output"; then
		unmeasurable+=("$path (missing telemetry; compiler crashed or exited early)")
		continue
	fi
	for counter in "${!RUNNER_MIGRATION_COUNTER_VALUES[@]}"; do
		compiled_values["$path"$'\t'"$counter"]="${RUNNER_MIGRATION_COUNTER_VALUES[$counter]}"
	done
done

for i in "${!entry_paths[@]}"; do
	path="${entry_paths[$i]}"
	counter="${entry_counters[$i]}"
	baseline="${entry_baselines[$i]}"
	key="$path"$'\t'"$counter"
	actual="${compiled_values[$key]-}"
	if [ -z "$actual" ]; then
		unmeasurable+=("$path ($counter telemetry missing)")
		continue
	fi
	status=$(runner_migration_counter_baseline_status "$actual" "$baseline")
	printf '%s  %s=%s  baseline=%s  [%s]\n' "$path" "$counter" "$actual" "$baseline" "$status"
	case "$status" in
		Regressed) regressions+=("$path [$counter]: baseline $baseline, now $actual") ;;
		Improved) improvements+=("$path [$counter]: baseline $baseline, now $actual") ;;
	esac
done

if [ "$update_baseline" -eq 1 ]; then
	if [ "${#unmeasurable[@]}" -gt 0 ]; then
		printf 'Refusing to update the baseline while corpus entries are unmeasurable:\n' >&2
		printf '  %s\n' "${unmeasurable[@]}" >&2
		exit 1
	fi
	{
		printf '# FlashCpp migration counter baseline (path<TAB>counter<TAB>count).\n'
		for key in $(printf '%s\n' "${!compiled_values[@]}" | sort); do
			IFS=$'\t' read -r path counter <<< "$key"
			printf '%s\t%s\t%s\n' "$path" "$counter" "${compiled_values[$key]}"
		done
	} > "$baseline_path"
	printf 'Baseline updated: %s\n' "$baseline_path"
	exit 0
fi

printf '\n'
if [ "${#unmeasurable[@]}" -gt 0 ]; then
	printf '=== Unmeasurable corpus entries ===\n'
	printf '  %s\n' "${unmeasurable[@]}"
fi
if [ "${#regressions[@]}" -gt 0 ]; then
	printf '=== Counter regressions ===\n'
	printf '  %s\n' "${regressions[@]}"
fi
if [ "${#improvements[@]}" -gt 0 ]; then
	printf 'Improvements recorded (lower the baseline with --update-baseline):\n'
	printf '  %s\n' "${improvements[@]}"
fi

if [ "${#regressions[@]}" -gt 0 ] || [ "${#unmeasurable[@]}" -gt 0 ]; then
	printf '\nRESULT: FAILED - migration counters moved in the wrong direction or became unmeasurable\n'
	exit 1
fi
printf 'RESULT: OK - all migration counters within baseline\n'
