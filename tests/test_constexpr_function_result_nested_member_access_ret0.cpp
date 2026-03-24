struct FunctionResultInner {
	int value;
	constexpr FunctionResultInner(int v) : value(v) {}
};

struct FunctionResultOuter {
	FunctionResultInner inner;
	constexpr FunctionResultOuter(int v) : inner(v) {}
};

constexpr FunctionResultOuter makeFunctionResultOuter() {
	return FunctionResultOuter(42);
}

static_assert(makeFunctionResultOuter().inner.value == 42);

int main() {
	return 0;
}
