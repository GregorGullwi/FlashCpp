template<class T> struct Owner {
	T data;
	T select(T value) const & noexcept {
		return value;
	}
	T* nested(T* value) const & noexcept {
		return value;
	}
};

template<class T> struct Key {
	T (Owner<T>::*pointer)(T) const & noexcept;
	int tag;
};

template<class T> struct ObjectKey {
	T Owner<T>::*pointer;
	int tag;
};

template<class T> struct NestedKey {
	T* (Owner<T>::*pointer)(T*) const & noexcept;
	int tag;
};

template<int Tag, class T> struct MixedKey {
	T (Owner<T>::*pointer)(T) const & noexcept;
	int tag;
};

template<Key<short> Value> struct ShortTag {};
template<Key<long long> Value> struct LongTag {};
template<ObjectKey<short> Value> struct ShortObjectTag {};
template<ObjectKey<long long> Value> struct LongObjectTag {};
template<NestedKey<short> Value> struct NestedTag {};
template<MixedKey<17, short> Value> struct MixedTag {};

template<class Left, class Right> struct SameType {
	static constexpr bool value = false;
};
template<class Type> struct SameType<Type, Type> {
	static constexpr bool value = true;
};

constexpr Key<short> short_key{&Owner<short>::select, 3};
constexpr Key<long long> long_key{&Owner<long long>::select, 5};
constexpr ObjectKey<short> short_object_key{&Owner<short>::data, 7};
constexpr ObjectKey<long long> long_object_key{&Owner<long long>::data, 11};
constexpr NestedKey<short> nested_key{&Owner<short>::nested, 13};
constexpr MixedKey<17, short> mixed_key{&Owner<short>::select, 17};

using ShortNamed = ShortTag<short_key>;
using ShortBraced = ShortTag<Key<short>{&Owner<short>::select, 3}>;
using LongNamed = LongTag<long_key>;
using LongBraced = LongTag<Key<long long>{&Owner<long long>::select, 5}>;
using ShortObjectNamed = ShortObjectTag<short_object_key>;
using ShortObjectBraced = ShortObjectTag<ObjectKey<short>{&Owner<short>::data, 7}>;
using LongObjectNamed = LongObjectTag<long_object_key>;
using LongObjectBraced = LongObjectTag<ObjectKey<long long>{&Owner<long long>::data, 11}>;
using NestedNamed = NestedTag<nested_key>;
using NestedBraced = NestedTag<NestedKey<short>{&Owner<short>::nested, 13}>;
using MixedNamed = MixedTag<mixed_key>;
using MixedBraced = MixedTag<MixedKey<17, short>{&Owner<short>::select, 17}>;

static_assert(SameType<ShortNamed, ShortBraced>::value);
static_assert(SameType<LongNamed, LongBraced>::value);
static_assert(SameType<ShortObjectNamed, ShortObjectBraced>::value);
static_assert(SameType<LongObjectNamed, LongObjectBraced>::value);
static_assert(SameType<NestedNamed, NestedBraced>::value);
static_assert(SameType<MixedNamed, MixedBraced>::value);

int main() {
	return 0;
}
