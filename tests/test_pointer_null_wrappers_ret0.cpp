const wchar_t* findWideChar(const wchar_t* text, wchar_t needle, unsigned long long count) {
	for (; 0 < count; ++text, --count) {
		if (*text == needle) {
			return text;
		}
	}

	return 0;
}

unsigned long long safeStrnlen(const char* text, unsigned long long count) {
	return text == 0 ? 0 : count;
}

int main() {
	const wchar_t wide_text[] = {L'a', L'b', 0};
	const wchar_t* found = findWideChar(wide_text, L'b', 2);
	const wchar_t* missing = findWideChar(wide_text, L'z', 2);

	if (found == 0 || *found != L'b') {
		return 1;
	}
	if (missing != 0) {
		return 2;
	}
	if (safeStrnlen(0, 9) != 0) {
		return 3;
	}
	if (safeStrnlen("abc", 3) != 3) {
		return 4;
	}

	return 0;
}
