// Regression: codegen for a class-template instantiation must not try to emit
// IR for an unused member constructor template whose parameter types are still
// unresolved template parameters.
//
// Reduced from the std::pair / <utility> path where beginStructDeclarationCodegen
// would walk every constructor of a concrete class-template instantiation and
// crash on the converting constructor template before any call instantiated it.

template<typename First, typename Second>
struct PairLike {
	First first;
	Second second;

	PairLike() : first(), second() {}
	PairLike(First a, Second b) : first(a), second(b) {}

	template<typename U1, typename U2>
	PairLike(U1&&, U2&&) : first(), second() {}
};

int main() {
	PairLike<int, int> value(3, 4);
	return value.first == 3 && value.second == 4 ? 0 : 1;
}
