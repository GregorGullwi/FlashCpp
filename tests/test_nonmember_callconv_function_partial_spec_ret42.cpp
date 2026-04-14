template <class Ty>
struct FunctionKind {
	static constexpr int value = 1;
};

template <class Ret, class... Args>
struct FunctionKind<Ret __cdecl(Args...) noexcept> {
	static constexpr int value = 42;
};

int main() {
	return FunctionKind<int __cdecl(double, char) noexcept>::value;
}
