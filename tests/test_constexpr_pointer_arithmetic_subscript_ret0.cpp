// Constexpr array-to-pointer decay must retain the array origin so pointer
// arithmetic and pointer subscripts can be evaluated.

constexpr int values[5] = {11, 22, 33, 44, 55};
constexpr const int* pointer = values + 1;

static_assert(*(values + 1) == 22);
static_assert(pointer[1] == 33);
static_assert(1[pointer] == 33);
static_assert(*(pointer + 2) == 44);

int main() {
	return 0;
}
