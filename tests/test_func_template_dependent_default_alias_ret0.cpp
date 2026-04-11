struct Payload {
	int value;
};

template<typename Type>
struct IdentityAlias {
	using type = Type;
};

template<typename Type, typename ReturnType = typename IdentityAlias<Type>::type>
ReturnType forwardValue(Type value) {
	return value;
}

int main() {
	Payload payload{42};
	return forwardValue(payload).value == 42 ? 0 : 1;
}
