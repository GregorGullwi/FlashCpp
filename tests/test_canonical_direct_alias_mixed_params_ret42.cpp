// A direct alias whose target selects one of several parameters must redirect
// correctly when an unused non-type parameter sits in between. The unused
// parameter is positional and must not leak into the selected type.
template <class T, int N>
using First = T;

template <int N, class T>
using Second = T;

int main() {
	First<char, 7> first = 0;
	Second<9, short> second = 0;
	return (sizeof(first) == sizeof(char) && sizeof(second) == sizeof(short)) ? 42 : 0;
}
