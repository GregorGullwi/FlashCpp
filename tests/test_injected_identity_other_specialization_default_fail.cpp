// Wrapper<long long> is a different specialization from the current
// Wrapper<char> instantiation. Rebinding it to the injected current class
// would make this deliberately false assertion pass.
template <class T>
struct Wrapper {
	T value;
	using value_type = T;

	template <class Other = Wrapper<long long>>
	struct MemberDefault {
		static_assert(
			sizeof(typename Other::value_type) == sizeof(char),
			"the explicit Wrapper<long long> default must remain distinct");
	};
};

int main() {
	Wrapper<char>::MemberDefault<> value;
	(void)value;
	return 0;
}
