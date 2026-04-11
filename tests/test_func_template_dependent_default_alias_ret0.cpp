template<typename Type>
struct IdentityAlias {
	using type = Type;
};

template<typename Type, typename ReturnType = typename IdentityAlias<Type>::type>
ReturnType forwardValue(Type value) {
	return value;
}

int main() {
	return forwardValue(42) == 42 ? 0 : 1;
}
