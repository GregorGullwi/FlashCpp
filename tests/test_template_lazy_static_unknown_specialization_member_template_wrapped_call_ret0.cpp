template <typename T>
struct Traits {
	template <typename U>
	struct Box {
		static constexpr int get() {
			return sizeof(U) == sizeof(T) ? 42 : 7;
		}
	};
};

template <typename T>
struct Holder {
	inline static int value = static_cast<int>(Traits<T>::template Box<T>::get());
};

int main() {
	return Holder<int>::value == 42 ? 0 : 1;
}
