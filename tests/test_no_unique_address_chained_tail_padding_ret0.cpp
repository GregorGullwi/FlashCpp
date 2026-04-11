#include <stddef.h>

struct Padded {
	short value;
	char tag;
};

struct Wrapper {
	[[no_unique_address]] Padded padded;
};

struct Chained {
	[[no_unique_address]] Wrapper wrapper;
	char tail;
};

static_assert(sizeof(Padded) == 4);
static_assert(sizeof(Wrapper) == 4);

#ifdef __FLASHCPP__
static_assert(sizeof(Chained) == 4);
static_assert(offsetof(Chained, tail) == 3);
#endif

int main() {
	Chained value{{{7, 9}}, 11};
	if (value.wrapper.padded.value != 7 || value.wrapper.padded.tag != 9) {
		return 1;
	}

	if (value.tail != 11) {
		return 2;
	}

	return 0;
}
