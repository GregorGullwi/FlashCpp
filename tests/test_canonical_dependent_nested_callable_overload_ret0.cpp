using IntCallback = int (*)(int);
using FloatCallback = float (*)(int);
using IntFactory = IntCallback (*)(int);
using FloatFactory = FloatCallback (*)(int);

struct IntSelection {
	static constexpr int value = 1;
};

struct FloatSelection {
	static constexpr int value = 2;
};

struct EllipsisSelection {
	static constexpr int value = 3;
};

IntSelection choose(IntFactory) { return {}; }
FloatSelection choose(FloatFactory) { return {}; }
EllipsisSelection choose(...) { return {}; }

template<class Type>
Type (*make_factory(int))(int) {
	return nullptr;
}

template<class Type>
auto select_factory() {
	return choose(&make_factory<Type>);
}

static_assert(__is_same(decltype(choose(&make_factory<int>)), IntSelection));
static_assert(__is_same(decltype(choose(&make_factory<float>)), FloatSelection));
static_assert(__is_same(decltype(select_factory<int>()), IntSelection));
static_assert(__is_same(decltype(select_factory<float>()), FloatSelection));

int main() {
	return select_factory<int>().value + select_factory<float>().value - 3;
}
