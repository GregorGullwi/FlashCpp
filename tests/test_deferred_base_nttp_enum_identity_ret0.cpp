enum Kind {
	Small = 2,
	Large = 4
};

constexpr Kind LargeValue = Large;

template <Kind K>
struct enum_value {
	static constexpr int selected = 1;
};

template <>
struct enum_value<LargeValue> {
	static constexpr int selected = 33;
};

template <int N>
struct enum_value_int {
	static constexpr int selected = 99;
};

template <typename T>
struct enum_source {
	static constexpr Kind value = Large;
};

template <typename T>
struct use_enum : enum_value<enum_source<T>::value> {};

int main() {
	if (use_enum<int>::selected != 33) return 1;
	if (enum_value_int<4>::selected != 99) return 2;
	return 0;
}
