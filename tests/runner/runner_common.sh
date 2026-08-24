#!/usr/bin/env bash

runner_relevant_sources() {
	local repo_root="$1"
	find "$repo_root/src" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print
	local relative_path
	for relative_path in FlashCpp.vcxproj FlashCppMSVC.vcxproj Makefile build_flashcpp.bat; do
		[ -f "$repo_root/$relative_path" ] && printf '%s\n' "$repo_root/$relative_path"
	done
}

runner_binary_is_fresh() {
	local binary="$1"
	local repo_root="$2"
	local source
	while IFS= read -r source; do
		[ "$source" -nt "$binary" ] && { RUNNER_NEWEST_SOURCE="$source"; return 1; }
	done < <(runner_relevant_sources "$repo_root")
	RUNNER_NEWEST_SOURCE=""
	return 0
}

runner_test_kind() {
	local file_name="$1"
	local source_path="$2"
	local platform_exclusions=" $3 "
	local support_sources=" $4 "
	[[ "$platform_exclusions" == *" $file_name "* ]] && { printf 'platform-excluded'; return; }
	[[ "$support_sources" == *" $file_name "* ]] && { printf 'support-source'; return; }
	[[ "$file_name" == *_fail.cpp ]] && { printf 'compile-failure'; return; }
	if grep -qE '\b(int|void)[[:space:]]+main[[:space:]]*\(' "$source_path"; then
		printf 'runnable'
	else
		printf 'compile-only'
	fi
}

runner_expected_return_value() {
	local name="$1"
	if [[ "$name" =~ _ret([0-9]+)(\.cpp)?$ ]]; then
		printf '%s' "${BASH_REMATCH[1]}"
	else
		printf '0'
	fi
}

runner_linux_return_is_valid() {
	local value
	value=$(runner_expected_return_value "$1")
	[ "$value" -le 255 ]
}

runner_pie_mode() {
	case "${FLASHCPP_PIE_MODE:-auto}" in
		supported|unsupported) printf '%s' "$FLASHCPP_PIE_MODE"; return ;;
		auto) ;;
		*) printf 'invalid'; return 2 ;;
	esac
	command -v clang++ >/dev/null 2>&1 || { printf 'unsupported'; return; }
	local probe
	probe=$(mktemp)
	if printf 'int main() { return 0; }\n' | clang++ -x c++ -fPIE -pie -o "$probe" - >/dev/null 2>&1 &&
		{ { command -v readelf >/dev/null 2>&1 && readelf -h "$probe" >/dev/null 2>&1; } ||
		  { command -v file >/dev/null 2>&1 && file "$probe" | grep -q ELF; }; }; then
		printf 'supported'
	else
		printf 'unsupported'
	fi
	rm -f "$probe"
}

runner_ci_init() {
	local path="$1"
	[ -z "$path" ] && return
	mkdir -p "$(dirname "$path")"
	printf 'flashcpp-runner-v1\tmeta\tschema\t1\n' > "$path"
}

runner_ci_record() {
	local path="$1"
	shift
	local field record='flashcpp-runner-v1'
	for field in "$@"; do
		field=${field//$'\t'/ }
		field=${field//$'\r'/ }
		field=${field//$'\n'/ }
		record+=$'\t'"$field"
	done
	if [ -z "$path" ]; then
		printf '%s\n' "$record"
	else
		printf '%s\n' "$record" >> "$path"
	fi
}
