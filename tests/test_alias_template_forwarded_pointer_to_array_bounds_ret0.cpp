template <class T, int Elements>
using ArrayPointer = T(*)[Elements];

template <class T, int Elements>
using ForwardedArrayPointer = ArrayPointer<T, Elements>;

static_assert(__is_pointer(ForwardedArrayPointer<int, 3>));
static_assert(!__is_array(ForwardedArrayPointer<int, 3>));

int main() {
	int values[3] = {11, 17, 23};
	ForwardedArrayPointer<int, 3> pointer = &values;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if (sizeof(*pointer) != sizeof(values)) return 2;
	if ((*pointer)[2] != 23) return 3;
	(*pointer)[1] = 42;
	return values[1] == 42 ? 0 : 4;
}
