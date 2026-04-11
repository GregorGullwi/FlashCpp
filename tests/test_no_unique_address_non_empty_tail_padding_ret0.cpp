#include <cstddef>

struct Padded {
	short value;
	char tag;
};

struct Holder {
	[[no_unique_address]] Padded padded;
	char tail;
};

static_assert(sizeof(Padded) == 4);

#ifdef __FLASHCPP__
static_assert(sizeof(Holder) == 4);
static_assert(offsetof(Holder, tail) == 3);
#endif

int main() {
	Holder holder{{7, 9}, 11};
	if (sizeof(Padded) != 4) {
		return 1;
	}

	if (holder.padded.value != 7 || holder.padded.tag != 9 || holder.tail != 11) {
		return 1;
	}

	return 0;
}
