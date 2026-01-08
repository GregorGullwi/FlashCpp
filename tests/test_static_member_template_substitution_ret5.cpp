template <typename T>
struct Helper {
	using type = T;
};

template <typename T>
struct Holder {
	static constexpr typename Helper<T>::type value = 5;
};

int main() {
	return Holder<int>::value;
}
