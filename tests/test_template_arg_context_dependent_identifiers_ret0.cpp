// Dependent identifiers remain deferred, but their final type/value kind is
// selected from the target template parameter.

template <typename T>
struct TypeSlot {
	static constexpr int value = sizeof(T);
};

template <int V>
struct ValueSlot {
	static constexpr int value = V;
};

template <typename T, int N>
struct DependentUse {
	static constexpr int value = TypeSlot<T>::value + ValueSlot<N>::value;
};

int main() {
	if (DependentUse<int, 5>::value != 9) return 1;
	return 0;
}

