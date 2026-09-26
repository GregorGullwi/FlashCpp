int (*(*deep_pointer)[3])[4] = nullptr;
short* array_of_pointers[2] = {};

static_assert(__is_pointer(int (*(*)[3])[4]));
static_assert(!__is_array(int (*(*)[3])[4]));
static_assert(__is_array(decltype(array_of_pointers)));
static_assert(!__is_pointer(decltype(array_of_pointers)));
static_assert(__is_pointer(long (*)[7]));
static_assert(!__is_array(long (*)[7]));
static_assert(__is_array(char*[3]));
static_assert(!__is_pointer(char*[3]));

int main() {
	if (!__is_pointer(decltype(deep_pointer))) {
		return 1;
	}
	if (__is_array(decltype(deep_pointer))) {
		return 2;
	}
	if (!__is_array(decltype(array_of_pointers))) {
		return 3;
	}
	if (__is_pointer(decltype(array_of_pointers))) {
		return 4;
	}
	return 42;
}
