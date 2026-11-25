// A static-member array used as a pointer decays to the member's own storage.
// The previous lowering had no symbol for the qualified array origin and
// stored null, so reads and writes through the decayed pointer crashed. The
// non-constexpr inline-static forms verify both directions for an int array and
// a record-element array; the constexpr form verifies the same storage is used
// when the member is read through a constexpr pointer.

struct IntTable {
	static inline int values[3] = {1, 2, 3};
};

struct Item {
	int tag;
	long long weight;
};

struct ItemTable {
	static inline Item items[2] = {{4, 5}, {6, 7}};
};

struct ConstIntTable {
	static constexpr int values[3] = {8, 9, 10};
};

int* int_pointer = IntTable::values;
Item* item_pointer = ItemTable::items;
constexpr const int* const_int_pointer = ConstIntTable::values;

int main() {
	if (int_pointer != IntTable::values) {
		return 1;
	}
	if (*int_pointer != 1 || int_pointer[2] != 3) {
		return 2;
	}
	int_pointer[1] = 12;
	if (IntTable::values[1] != 12) {
		return 3;
	}
	if (item_pointer != ItemTable::items) {
		return 4;
	}
	if (item_pointer[0].tag != 4 || item_pointer[1].weight != 7) {
		return 5;
	}
	item_pointer[1].tag = 16;
	if (ItemTable::items[1].tag != 16) {
		return 6;
	}
	if (&item_pointer[1] != &ItemTable::items[1]) {
		return 7;
	}
	if (const_int_pointer[0] != 8 || const_int_pointer[2] != 10) {
		return 8;
	}
	return 0;
}
