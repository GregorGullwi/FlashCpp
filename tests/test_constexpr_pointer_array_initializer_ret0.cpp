// Constexpr pointer variables initialized directly from an array must retain
// the decayed pointer origin so direct dereference and pointer arithmetic use
// the same target as pointer subscripting.

constexpr int values[5] = {11, 22, 33, 44, 55};
constexpr const int* pointer = values;

static_assert(*pointer == 11);
static_assert(pointer[2] == 33);
static_assert(2[pointer] == 33);
static_assert(*(pointer + 3) == 44);

constexpr const int* offset_pointer = values + 1;

static_assert(*offset_pointer == 22);
static_assert(*(offset_pointer + 2) == 44);

int main() {
	return 0;
}
