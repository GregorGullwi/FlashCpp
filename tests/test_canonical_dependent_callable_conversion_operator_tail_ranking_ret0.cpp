using IntFunction = long (*)(int);
using CharFunction = long (*)(char);

struct IntSelection {
	char value;
};

struct CharSelection {
	char value[2];
};

IntSelection choose(IntFunction) {
	return {7};
}

CharSelection choose(CharFunction) {
	return {{11, 12}};
}

int choose(...) {
	return 19;
}

template <class Type>
using SourceFunction = long (*)(Type);

template <class Type>
using ChainedSourceFunction = SourceFunction<Type>;

template <class Type>
using NestedSourceFunction = ChainedSourceFunction<Type>;

template <class Type>
long target(Type value) {
	return static_cast<long>(value);
}

template <class Type>
struct Source {
	operator NestedSourceFunction<Type>() const {
		return &target<Type>;
	}
};

static_assert(sizeof(decltype(choose(Source<int>{}))) == sizeof(IntSelection));
static_assert(sizeof(decltype(choose(Source<char>{}))) == sizeof(CharSelection));

int main() {
	return sizeof(decltype(choose(Source<int>{}))) == sizeof(IntSelection) && sizeof(decltype(choose(Source<char>{}))) == sizeof(CharSelection) ? 0 : 1;
}
