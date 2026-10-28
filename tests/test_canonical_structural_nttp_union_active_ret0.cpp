struct Key {
	union {
		int first;
		int second;
	};
};

constexpr Key left{.first = 0};
constexpr Key right{.second = 0};

union Choice {
	int small;
	long long wide;
};

struct Holder {
	long long guard;
	Choice choice;
};

constexpr Holder holder_small{1, {.small = 7}};
constexpr Holder holder_wide{1, {.wide = 7}};

template<Key Value>
struct Base {
	short member;
};

struct Padding {
	long long guard;
};

struct LeftDerived : Padding, Base<left> {};
struct RightDerived : Padding, Base<right> {};

template<class A, class B>
struct Same {
	static constexpr bool value = false;
};

template<class A>
struct Same<A, A> {
	static constexpr bool value = true;
};

template<Key Value>
struct KeyTag {
	static constexpr int value = 0;
};

template<>
struct KeyTag<left> {
	static constexpr int value = 1;
};

template<>
struct KeyTag<right> {
	static constexpr int value = 2;
};

template<Holder Value>
struct HolderTag {
	static constexpr int value = 0;
};

template<>
struct HolderTag<holder_small> {
	static constexpr int value = 4;
};

template<>
struct HolderTag<holder_wide> {
	static constexpr int value = 8;
};

static_assert(!Same<KeyTag<left>, KeyTag<right>>::value);
static_assert(Same<KeyTag<Key{.first = 0}>, KeyTag<left>>::value);
static_assert(Same<KeyTag<Key{.second = 0}>, KeyTag<right>>::value);
static_assert(!Same<HolderTag<holder_small>, HolderTag<holder_wide>>::value);
static_assert(Same<HolderTag<Holder{1, {.small = 7}}>, HolderTag<holder_small>>::value);

static_assert(!Same<Base<left>, Base<right>>::value);

int main() {
	LeftDerived left_object;
	RightDerived right_object;
	left_object.guard = 12345678901LL;
	right_object.guard = 12345678902LL;
	left_object.member = 11;
	right_object.member = 22;
	Base<left>* left_base = &left_object;
	Base<right>* right_base = &right_object;
	return KeyTag<left>::value == 1 && KeyTag<right>::value == 2 &&
			HolderTag<holder_small>::value == 4 && HolderTag<holder_wide>::value == 8 &&
			left_base->member == 11 && right_base->member == 22 &&
			left_object.guard == 12345678901LL && right_object.guard == 12345678902LL
		? 0 : 1;
}
