template <typename T>
struct Current {
	template <typename U>
	struct Box {
		static constexpr int get() {
			return sizeof(U) == sizeof(int) ? 42 : 7;
		}
	};

	inline static int value = Current<T>::Box<T>::get();
};

int main() {
	return Current<int>::value == 42 ? 0 : 1;
}
