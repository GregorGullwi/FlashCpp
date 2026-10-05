// An alias-template specialization of decltype(&function<T>) must compare
// identical to the same function-template address written directly. Alias
// substitution rebuilds the address-of operand as a qualified identifier,
// which previously took the generic address-of path and added a second pointer
// level, so SameType<Type, Type> rejected the pair even though __is_same
// already saw one canonical TypeId. The fix returns the designator's
// function-pointer type for a global function template-id.
template <class Type>
int function() {
	return 0;
}

template <class Type>
char convert(Type) {
	return 0;
}

struct Payload {
	int value;
};

template <class Type>
Payload wrap(Type*) {
	return Payload{0};
}

template <class Type>
using FunctionAddress = decltype(&function<Type>);

template <class Type>
using ConvertAddress = decltype(&convert<Type>);

template <class Type>
using WrapAddress = decltype(&wrap<Type>);

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

static_assert(SameType<FunctionAddress<int>, decltype(&function<int>)>::value);
static_assert(SameType<ConvertAddress<double>, decltype(&convert<double>)>::value);
static_assert(SameType<WrapAddress<Payload>, decltype(&wrap<Payload>)>::value);

int main() {
	return SameType<FunctionAddress<char>, decltype(&function<char>)>::value &&
			SameType<ConvertAddress<int>, decltype(&convert<int>)>::value &&
			SameType<WrapAddress<int>, decltype(&wrap<int>)>::value
		? 0
		: 1;
}
