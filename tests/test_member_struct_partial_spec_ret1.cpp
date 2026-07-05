// Test member struct template partial specialization
// This pattern is used in <type_traits> at line 1841

class TestClass {
public:
	// Primary template
	template <bool Active, typename...>
	struct List {
		static constexpr int size = 0;
	};

	// Partial specialization with static constexpr member
	template <typename T, typename... Rest>
	struct List<true, T, Rest...> : List<true, Rest...> {
		static constexpr int size = 1;
	};
};

int main() {
	// Instantiate template - the static constexpr members now parse correctly
	TestClass::List<true, int, char, float> list;
	return decltype(list)::size;
}
