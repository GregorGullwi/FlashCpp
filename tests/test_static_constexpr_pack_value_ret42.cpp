// Validates constexpr evaluation of static members in template instantiations
// with parameter pack dependent expressions.

template<typename... Ts>
struct base {
	static constexpr int value = static_cast<int>(sizeof...(Ts)) * 2 + 40;
};

template<typename... Ts>
struct derived : base<Ts...> { };

int main() {
	return derived<int>::value;
}
