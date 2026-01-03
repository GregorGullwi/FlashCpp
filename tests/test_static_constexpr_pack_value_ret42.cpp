// Validates constexpr evaluation of static members in template instantiations
// with parameter pack dependent expressions.

template<typename... Ts>
struct base {
	static const int value = 42;
};

template<typename... Ts>
struct derived : base<Ts...> { };

int main() {
	return derived<int>::value;
}
