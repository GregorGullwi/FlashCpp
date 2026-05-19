template <typename T>
struct Base {
	static int get() {
		return sizeof(T) == sizeof(int) ? 42 : 7;
	}
};

template <typename T>
struct UseBase {
	inline static int value = Base<T>::get();
};

int main() {
	return UseBase<int>::value == 42 ? 0 : 1;
}
