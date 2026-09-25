// [basic.compound]: __is_pointer classifies the outermost ordered
// declarator component even when pointer and array wrappers cannot be
// represented by the legacy flat projection.
struct Payload {
	short value;
};

int (*(*interleaved)[3])[4] = nullptr;
long values[2]{};

int main() {
	if (!__is_pointer(decltype(interleaved))) {
		return 1;
	}
	if (!__is_pointer(Payload*)) {
		return 2;
	}
	if (__is_pointer(decltype(values))) {
		return 3;
	}
	return 42;
}
