// Test __builtin_memcmp support in template bodies.
// libstdc++ uses this builtin in headers such as <atomic> and <latch>, so it
// must be recognized during phase-1 lookup and lowered like the libc memcmp.

#if !__has_builtin(__builtin_memcmp)
#error __builtin_memcmp should be reported as supported
#endif

template <typename T>
int compare_prefix(const char* lhs, const char* rhs) {
	return __builtin_memcmp(lhs, rhs, sizeof(T));
}

int main() {
	if (compare_prefix<int>("same", "same") != 0) {
		return 1;
	}
	if (compare_prefix<int>("abca", "abcb") >= 0) {
		return 2;
	}
	if (compare_prefix<int>("abcb", "abca") <= 0) {
		return 3;
	}
	return 0;
}
