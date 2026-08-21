template <class T>
struct Wrapper {
	using value_type = T;
	using wide_type = Wrapper<long long>;

	template <class Self = Wrapper,
			  int Size = sizeof(typename Self::value_type)>
	int size() {
		return Size;
	}

	wide_type keep_distinct(wide_type value) {
		return value;
	}
};

int main() {
	Wrapper<char> small;
	Wrapper<long long> large;
	Wrapper<long long> kept = small.keep_distinct(large);
	return small.size() == 1 &&
			large.size() == sizeof(long long) &&
			kept.size() == sizeof(long long)
		? 0
		: 1;
}
