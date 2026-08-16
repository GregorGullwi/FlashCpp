// Regression: pack expansions in a new-expression's constructor arguments
// must be expanded during concrete function-template instantiation. Leaving
// the parser-only PackExpansionExprNode in the materialized body makes it
// reach semantic analysis and IR generation.

struct MixedValue {
	long long wide;
	short narrow;

	MixedValue(long long first, short second)
		: wide(first), narrow(second) {}
};

void* operator new(unsigned long long, void* address) noexcept {
	return address;
}

template <typename T, typename... Args>
T* createValue(Args... args) {
	return new T(args...);
}

template <typename T, typename... Args>
T* constructAt(T* location, Args... args) {
	return new (location) T(args...);
}

int main() {
	MixedValue* value = createValue<MixedValue>(40LL, static_cast<short>(2));
	int result = value->wide + value->narrow == 42 ? 0 : 1;
	delete value;
	MixedValue placed(0, 0);
	constructAt(&placed, 60LL, static_cast<short>(-18));
	if (placed.wide + placed.narrow != 42) {
		return 2;
	}
	return result;
}
