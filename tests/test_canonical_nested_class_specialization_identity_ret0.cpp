template <class T>
struct Outer {
	struct Key {
		T value;
	};

	struct Other {
		T value;
	};

};

template <class T>
struct OtherOuter {
	struct Key {
		T value;
	};
};

struct Plain { struct Key { short value; }; };
using PlainKey = Plain::Key;

using ShortKey = Outer<short>::Key;
using WideKey = Outer<long long>::Key;
using ShortAgain = Outer<short>::Key;
using ShortChain = ShortKey;
using ShortPointer = ShortChain*;
using OtherKey = Outer<short>::Other;
using ForeignKey = OtherOuter<short>::Key;

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

static_assert(SameType<ShortKey, ShortAgain>::value);
static_assert(!SameType<ShortKey, WideKey>::value);
static_assert(!SameType<ShortKey, OtherKey>::value);
static_assert(!SameType<ShortKey, ForeignKey>::value);

int main() {
	ShortKey short_value{3};
	WideKey wide_value{7};
	ShortAgain short_again{5};
	OtherKey other_value{11};
	ForeignKey foreign_value{13};
	return short_value.value == 3 && wide_value.value == 7 && short_again.value == 5 && other_value.value == 11 && foreign_value.value == 13 ? 0 : 1;
}
