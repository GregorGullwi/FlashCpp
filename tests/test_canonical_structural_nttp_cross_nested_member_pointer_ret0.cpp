template<class T, int N> struct Outer {
	struct Target {
		T data;
		T other;
		T select(T value) const & noexcept {
			return value;
		}
	};
	using TargetAlias = Target;
	struct Key {
		T TargetAlias::*object;
		T (TargetAlias::*function)(T) const & noexcept;
		T tag;
		T TargetAlias::*objects[2];
	};
	struct Level {
		T data;
		struct Child {
			T Level::*object;
			T tag;
		};
	};
};

using ShortKey1 = Outer<short, 1>::Key;
using LongKey = Outer<long long, 1>::Key;
using ShortChild1 = Outer<short, 1>::Level::Child;
using LongChild = Outer<long long, 1>::Level::Child;
using FixedTarget = Outer<short, 1>::Target;

template<class Unused> struct Wrapper {
	struct Key {
		short FixedTarget::*pointer;
	};
};
using FixedKey = Wrapper<long long>::Key;

template<class Left, class Right> struct SameType {
	static constexpr bool value = false;
};
template<FixedKey Value> struct FixedTag {};
template<class Type> struct SameType<Type, Type> {
	static constexpr bool value = true;
};

template<ShortKey1 Value> struct ShortTag {};
template<LongKey Value> struct LongTag {};
template<ShortChild1 Value> struct ChildTag {};
template<LongChild Value> struct LongChildTag {};

constexpr ShortKey1 short_key{&Outer<short, 1>::Target::data, &Outer<short, 1>::Target::select, 3,
	{&Outer<short, 1>::Target::data, nullptr}};
constexpr LongKey long_key{&Outer<long long, 1>::Target::data, &Outer<long long, 1>::Target::select, 5,
	{&Outer<long long, 1>::Target::data, nullptr}};
constexpr ShortChild1 short_child{&Outer<short, 1>::Level::data, 7};
constexpr LongChild long_child{&Outer<long long, 1>::Level::data, 11};

using ShortNamed = ShortTag<short_key>;
using ShortBraced = ShortTag<ShortKey1{&Outer<short, 1>::Target::data, &Outer<short, 1>::Target::select, 3,
	{&Outer<short, 1>::Target::data, nullptr}}>;
using ShortRepeatAlias = ShortTag<ShortKey1{&Outer<short, 1>::Target::data, &Outer<short, 1>::Target::select, 3,
	{&Outer<short, 1>::Target::data, nullptr}}>;
using LongNamed = LongTag<long_key>;
using LongBraced = LongTag<LongKey{&Outer<long long, 1>::Target::data, &Outer<long long, 1>::Target::select, 5,
	{&Outer<long long, 1>::Target::data, nullptr}}>;
using ChangedTarget = ShortTag<ShortKey1{&Outer<short, 1>::Target::other, &Outer<short, 1>::Target::select, 3,
	{&Outer<short, 1>::Target::other, nullptr}}>;
using ShortChildNamed = ChildTag<short_child>;
using ShortChildBraced = ChildTag<ShortChild1{&Outer<short, 1>::Level::data, 7}>;
using LongChildNamed = LongChildTag<long_child>;
using LongChildBraced = LongChildTag<LongChild{&Outer<long long, 1>::Level::data, 11}>;
constexpr FixedKey fixed{&FixedTarget::data};
using FixedNamed = FixedTag<fixed>;
using FixedBraced = FixedTag<FixedKey{&FixedTarget::data}>;

static_assert(SameType<ShortNamed, ShortBraced>::value);
static_assert(SameType<ShortNamed, ShortRepeatAlias>::value);
static_assert(SameType<LongNamed, LongBraced>::value);
static_assert(!SameType<ShortNamed, ChangedTarget>::value);
static_assert(SameType<ShortChildNamed, ShortChildBraced>::value);
static_assert(SameType<LongChildNamed, LongChildBraced>::value);
static_assert(SameType<FixedNamed, FixedBraced>::value);

int main() {
	return short_key.tag == 3 && long_key.tag == 5 && short_child.tag == 7 && long_child.tag == 11 ? 0 : 1;
}
