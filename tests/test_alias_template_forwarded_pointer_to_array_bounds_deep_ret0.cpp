// A pointer-to-array alias forwarded through several alias templates must keep
// the pointee array extent. A prior known issue recorded that a three-hop
// forwarding chain could lose the bound even when one forwarding alias
// preserved it, so this pins the extent across an eight-hop chain.
template <class T, int Elements>
using A0 = T (*)[Elements];

template <class T, int Elements>
using A1 = A0<T, Elements>;

template <class T, int Elements>
using A2 = A1<T, Elements>;

template <class T, int Elements>
using A3 = A2<T, Elements>;

template <class T, int Elements>
using A4 = A3<T, Elements>;

template <class T, int Elements>
using A5 = A4<T, Elements>;

template <class T, int Elements>
using A6 = A5<T, Elements>;

template <class T, int Elements>
using A7 = A6<T, Elements>;

static_assert(__is_pointer(A3<int, 3>), "a forwarded pointer-to-array stays a pointer");
static_assert(!__is_array(A3<int, 3>), "a forwarded pointer-to-array is not itself an array");
static_assert(__is_pointer(A7<int, 3>), "a deeply forwarded pointer-to-array stays a pointer");

int main() {
	int values[3] = {11, 17, 23};
	A3<int, 3> shallow = &values;
	if (sizeof(*shallow) != sizeof(values)) return 1;
	A7<int, 3> deep = &values;
	if (sizeof(*deep) != sizeof(values)) return 2;
	if ((*deep)[2] != 23) return 3;
	(*deep)[1] = 42;
	return values[1] == 42 ? 0 : 4;
}
