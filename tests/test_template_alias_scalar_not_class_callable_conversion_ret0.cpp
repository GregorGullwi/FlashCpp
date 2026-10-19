using IntFunction = int (*)(int);

struct FunctionChoice {
	char value;
};

struct EllipsisChoice {
	char value[2];
};

FunctionChoice choose(IntFunction) {
	return {7};
}

EllipsisChoice choose(...) {
	return {{11, 12}};
}

template <class Type>
struct IdentityWrapper {
	using type = Type;
};

template <class Type>
using Identity = typename IdentityWrapper<Type>::type;

template <class Type>
auto select(Type value) {
	Identity<Type> alias_value = value;
	return choose(alias_value);
}

static_assert(sizeof(decltype(select<int>(1))) == sizeof(EllipsisChoice));

struct RecordTarget {
	long long value;
	RecordTarget(short input) : value(input) {}
};

template <class Type>
using ValueAlias = Type;

template <class Type>
long long construct(short value) {
	ValueAlias<Type> target = value;
	return target.value;
}

int main() {
	return sizeof(decltype(select<int>(1))) == sizeof(EllipsisChoice) && construct<RecordTarget>(23) == 23 ? 0 : 1;
}
