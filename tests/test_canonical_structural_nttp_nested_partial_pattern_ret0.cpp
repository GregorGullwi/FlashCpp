template<class T> struct Box {};
template<class T> struct OtherBox {};
template<class T> struct Key;
template<class T> struct ObjectKey;
template<class T> struct NestedKey;

template<class U> struct Key<Box<U>>;

template<class T> struct Owner {
	T data;
	T select(T value) const & noexcept {
		return value;
	}
	T* nested(T* value) const & noexcept {
		return value;
	}
};

template<class T> struct Key<Box<T>> {
	T (Owner<T>::*pointer)(T) const & noexcept;
	int tag;
};
template<class T> struct Key<OtherBox<T>> {
	T (Owner<T>::*pointer)(T) const & noexcept;
	int tag;
};
template<class T> struct ObjectKey<Box<T>> {
	T Owner<T>::*pointer;
	int tag;
};
template<class T> struct NestedKey<Box<Box<T>>> {
	T* (Owner<T>::*pointer)(T*) const & noexcept;
	int tag;
};

template<Key<Box<short>> Value> struct ShortTag {};
template<Key<Box<long long>> Value> struct LongTag {};
template<ObjectKey<Box<short>> Value> struct ShortObjectTag {};
template<ObjectKey<Box<long long>> Value> struct LongObjectTag {};
template<NestedKey<Box<Box<short>>> Value> struct NestedTag {};
template<Key<OtherBox<short>> Value> struct OtherTag {};

template<class Left, class Right> struct SameType {
	static constexpr bool value = false;
};
template<class Type> struct SameType<Type, Type> {
	static constexpr bool value = true;
};

constexpr Key<Box<short>> short_key{&Owner<short>::select, 3};
constexpr Key<Box<long long>> long_key{&Owner<long long>::select, 5};
constexpr ObjectKey<Box<short>> short_object_key{&Owner<short>::data, 7};
constexpr ObjectKey<Box<long long>> long_object_key{&Owner<long long>::data, 11};
constexpr NestedKey<Box<Box<short>>> nested_key{&Owner<short>::nested, 13};
constexpr Key<OtherBox<short>> other_key{&Owner<short>::select, 3};

using ShortNamed = ShortTag<short_key>;
using ShortBraced = ShortTag<Key<Box<short>>{&Owner<short>::select, 3}>;
using LongNamed = LongTag<long_key>;
using LongBraced = LongTag<Key<Box<long long>>{&Owner<long long>::select, 5}>;
using ShortObjectNamed = ShortObjectTag<short_object_key>;
using ShortObjectBraced = ShortObjectTag<ObjectKey<Box<short>>{&Owner<short>::data, 7}>;
using LongObjectNamed = LongObjectTag<long_object_key>;
using LongObjectBraced = LongObjectTag<ObjectKey<Box<long long>>{&Owner<long long>::data, 11}>;
using NestedNamed = NestedTag<nested_key>;
using NestedBraced = NestedTag<NestedKey<Box<Box<short>>>{&Owner<short>::nested, 13}>;
using OtherNamed = OtherTag<other_key>;
using OtherBraced = OtherTag<Key<OtherBox<short>>{&Owner<short>::select, 3}>;

static_assert(SameType<ShortNamed, ShortBraced>::value);
static_assert(SameType<LongNamed, LongBraced>::value);
static_assert(SameType<ShortObjectNamed, ShortObjectBraced>::value);
static_assert(SameType<LongObjectNamed, LongObjectBraced>::value);
static_assert(SameType<NestedNamed, NestedBraced>::value);
static_assert(SameType<OtherNamed, OtherBraced>::value);
static_assert(!SameType<ShortNamed, OtherNamed>::value);

int main() {
	return 0;
}