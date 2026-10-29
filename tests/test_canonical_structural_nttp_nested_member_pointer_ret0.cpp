template<class T> struct Owner {
	T data;
	T other;
	T select(T value) const & noexcept {
		return value;
	}
};

struct Prefix {
	short prefix;
};

template<class T> struct Outer {
	struct Key : Prefix {
		T Owner<T>::*object;
		T (Owner<T>::*function)(T) const & noexcept;
		T tag;
	};
	struct SelfKey {
		T data;
		T SelfKey::*pointer;
		T (SelfKey::*function)(T) const & noexcept;
		T SelfKey::*pointers[2];
		T select(T value) const & noexcept {
			return value;
		}
	};
};

using ShortKey = Outer<short>::Key;
using LongKey = Outer<long long>::Key;
using ShortAgain = Outer<short>::Key;
using ShortSelf = Outer<short>::SelfKey;
using LongSelf = Outer<long long>::SelfKey;

constexpr ShortKey namedshort{{1}, &Owner<short>::data, &Owner<short>::select, 3};
constexpr LongKey namedlong{{2}, &Owner<long long>::data, &Owner<long long>::select, 5};
constexpr ShortSelf namedself{7, &ShortSelf::data, &ShortSelf::select, {&ShortSelf::data, nullptr}};

template<ShortKey value> struct ShortTag {};
template<LongKey value> struct LongTag {};
template<ShortSelf value> struct SelfTag {};
template<LongSelf value> struct LongSelfTag {};
template<class Left, class Right> struct SameType {
	static constexpr bool value = false;
};
template<class Type> struct SameType<Type, Type> {
	static constexpr bool value = true;
};

using ShortNamed = ShortTag<namedshort>;
using ShortBraced = ShortTag<ShortKey{{1}, &Owner<short>::data, &Owner<short>::select, 3}>;
using ShortRepeatAlias = ShortTag<ShortAgain{{1}, &Owner<short>::data, &Owner<short>::select, 3}>;
using LongNamed = LongTag<namedlong>;
using LongBraced = LongTag<LongKey{{2}, &Owner<long long>::data, &Owner<long long>::select, 5}>;
using SelfNamed = SelfTag<namedself>;
using SelfBraced = SelfTag<ShortSelf{7, &ShortSelf::data, &ShortSelf::select, {&ShortSelf::data, nullptr}}>;
using LongSelfNamed = LongSelfTag<LongSelf{9, &LongSelf::data, &LongSelf::select, {&LongSelf::data, nullptr}}>;
using LongSelfBraced = LongSelfTag<LongSelf{9, &LongSelf::data, &LongSelf::select, {&LongSelf::data, nullptr}}>;
using ChangedObject = ShortTag<ShortKey{{1}, &Owner<short>::other, &Owner<short>::select, 3}>;

static_assert(SameType<ShortNamed, ShortBraced>::value);
static_assert(SameType<ShortNamed, ShortRepeatAlias>::value);
static_assert(SameType<LongNamed, LongBraced>::value);
static_assert(SameType<SelfNamed, SelfBraced>::value);
static_assert(SameType<LongSelfNamed, LongSelfBraced>::value);
static_assert(!SameType<ShortNamed, ChangedObject>::value);

int main() {
	return namedshort.tag == 3 && namedlong.tag == 5 ? 0 : 1;
}
