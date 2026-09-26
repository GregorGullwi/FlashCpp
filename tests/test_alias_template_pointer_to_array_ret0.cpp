template <class T>
using PointerToArray = T(*)[3];

static_assert(__is_pointer(PointerToArray<int>));
static_assert(!__is_array(PointerToArray<int>));

int main() {
	int values[3] = {11, 17, 23};
	PointerToArray<int> pointer = &values;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if ((*pointer)[1] != 17) return 2;
	(*pointer)[2] = 42;
	if (values[2] != 42) return 3;

	long long long_values[3] = {101LL, 202LL, 303LL};
	PointerToArray<long long> long_pointer = &long_values;
	if (sizeof(long_pointer) != sizeof(void*)) return 4;
	if ((*long_pointer)[1] != 202LL) return 5;
	(*long_pointer)[2] = 404LL;
	return long_values[2] == 404LL ? 0 : 6;
}
