// Per-level pointer cv and pointer presence. The return value changes if an
// outer const pointer is treated as a plain pointer, if a pointer member loses
// its pointee pointer, or if a function pointer is no longer a pointer.
int classify_pointer(int*) {
	return 1;
}

int classify_pointer(int* const*) {
	return 2;
}

struct Box {
	int* value;
};

int main() {
	int value = 42;
	int* inner = &value;
	int* const* outer_const = &inner;
	if (classify_pointer(inner) != 1) {
		return 1;
	}
	if (classify_pointer(outer_const) != 2) {
		return 2;
	}
	if (__is_same(const int* const*, int* const*) || __is_same(const int* const*, int**)) {
		return 3;
	}
	if (!__is_pointer(int*) || __is_pointer(int)) {
		return 5;
	}
	if (!__is_pointer(int (*)(int)) || __is_same(int (*)(int), int)) {
		return 6;
	}
	Box box{};
	box.value = &value;
	int* through_member = box.*(&Box::value);
	if (*through_member != 42) {
		return 7;
	}
	return 42;
}
