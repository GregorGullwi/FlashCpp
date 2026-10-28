struct Unqueried { int select(short value); };

struct Object {
	int select(short value);
	long long select(long long value);
	int qualified() const & noexcept;
	static int static_select(short value);
};

template<class T> struct Owner {
	T select(T value);
};

struct Key {
	int (Object::*pointer)(short);
	int tag;
};

struct LongKey {
	long long (Object::*pointer)(long long);
	int tag;
};

struct OtherObject {
	int select(short value);
};

struct OtherKey {
	int (OtherObject::*pointer)(short);
	int tag;
};

struct QualifiedKey {
	int (Object::*pointer)() const & noexcept;
	int tag;
};

struct GenericKey {
	short (Owner<short>::*pointer)(short);
	int tag;
};

constexpr Key selected{&Object::select, 7};
constexpr OtherKey other_selected{&OtherObject::select, 7};
constexpr QualifiedKey qualified{&Object::qualified, 11};
constexpr GenericKey generic{&Owner<short>::select, 9};

template<Key Value> struct KeyTag {};
template<LongKey Value> struct LongKeyTag {};
template<OtherKey Value> struct OtherKeyTag {};
template<QualifiedKey Value> struct QualifiedTag {};
template<GenericKey Value> struct GenericTag {};

using Named = KeyTag<selected>;
using Braced = KeyTag<Key{&Object::select, 7}>;
using OtherOverload = LongKeyTag<LongKey{&Object::select, 7}>;
using OtherOwner = OtherKeyTag<other_selected>;
using Qualified = QualifiedTag<qualified>;
using GenericNamed = GenericTag<generic>;
using GenericBraced = GenericTag<GenericKey{&Owner<short>::select, 9}>;

template<class Left, class Right> struct SameType {
	static constexpr bool value = false;
};

template<class Type> struct SameType<Type, Type> {
	static constexpr bool value = true;
};

static_assert(SameType<Named, Braced>::value);
static_assert(!SameType<Named, OtherOverload>::value);
static_assert(!SameType<Named, OtherOwner>::value);
static_assert(SameType<GenericNamed, GenericBraced>::value);

int main() {
	return 0;
}
