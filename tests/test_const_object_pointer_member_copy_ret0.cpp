struct PointerRecord { int* data; short index; };
struct PointerArrayRecord { int* data[2]; };
struct FixedPointerRecord { int* const data; };
struct Receiver {
	int* data;
	int index;
	Receiver(int* pointer, int offset) : data(pointer), index(offset) {}
};

Receiver copy(const PointerRecord& source) { return Receiver(source.data, source.index); }
int select(int*) { return 0; }
int select(const int*) { return 1; }

int main() {
	int value = 42;
	PointerRecord source{&value, 3};
	Receiver result = copy(source);
	const PointerRecord& read_only = source;
	if (select(read_only.data) != 0) { return 2; }
	PointerArrayRecord array{{&value, &value}};
	const PointerArrayRecord& array_view = array;
	if (select(array_view.data[1]) != 0) { return 4; }
	FixedPointerRecord fixed{&value};
	static_assert(__is_same(decltype((source.data)), int*&));
	static_assert(__is_same(decltype((read_only.data)), int* const&));
	static_assert(__is_same(decltype((array_view.data[0])), int* const&));
	static_assert(__is_same(decltype((fixed.data)), int* const&));
	return result.data == &value && *result.data == 42 && result.index == 3 ? 0 : 1;
}
