template <class T>
struct ArrayExtent;

template <class T, int Bound>
struct ArrayExtent<T[Bound]> {
	static constexpr int value = Bound;
};

int main() {
	return ArrayExtent<long long[7]>::value == 7
		? 0
		: 1;
}
