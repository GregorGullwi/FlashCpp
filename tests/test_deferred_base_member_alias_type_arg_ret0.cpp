template <typename T>
struct choose_base {
	using type = T;
	static constexpr int count = sizeof(T);
};

template <typename T, int N>
struct array_holder {
	T values[N];
};

template <typename T>
struct deferred_array
	: array_holder<typename choose_base<T>::type, choose_base<T>::count> {
	int size() const {
		return static_cast<int>(sizeof(this->values) / sizeof(this->values[0]));
	}
};

int main() {
	deferred_array<unsigned short> value;
	return value.size() == choose_base<unsigned short>::count ? 0 : 1;
}
