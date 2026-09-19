template <class T, int N>
using Bounded = T[N];
template <class T, int N>
using SuccessorBound = T[N + 1];
int main() {
	Bounded<int, 3> values = {1, 2, 3};
	SuccessorBound<char, 2> bytes = {'a', 'b', 'c'};
	return sizeof(values) == 3 * sizeof(int) && values[2] == 3 &&
		sizeof(bytes) == 3 && bytes[2] == 'c' ? 42 : 0;
}
