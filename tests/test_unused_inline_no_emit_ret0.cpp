// Unused free inline definitions must not be ODR-emitted into the object file.
// If they were, the undefined external would fail at link time even though main
// never calls the helper — the same class of failure as UCRT wmemchr under
// <limits> when every header inline was eagerly codegen'd.

extern "C" int undefined_simd_helper();

inline int unused_helper_that_would_pull_undefined() {
	return undefined_simd_helper();
}

int main() {
	return 0;
}
