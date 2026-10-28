template<class T> struct Payload { T value; };
struct Padding { long long guard; };
template<class T> struct Key : Padding, Payload<T> { unsigned short tag; };
struct Wrapper { Key<short> nested; };
template<class T> struct ArrayWrapper { Key<T> elements[2]; };

constexpr Key<short> key{{12345678901LL}, {-7}, 42};
constexpr Wrapper wrapper{key};
constexpr ArrayWrapper<short> array{{key, {{9}, {8}, 7}}};

template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
template<Key<short> Value> struct Tag {};
template<Wrapper Value> struct NestedTag {};
template<ArrayWrapper<short> Value> struct ArrayTag {};

static_assert(Same<Tag<key>, Tag<Key<short>{{12345678901LL}, {-7}, 42}>>::value);
static_assert(!Same<Tag<key>, Tag<Key<short>{{12345678901LL}, {-8}, 42}>>::value);
static_assert(Same<NestedTag<wrapper>, NestedTag<Wrapper{key}>>::value);
static_assert(Same<ArrayTag<array>, ArrayTag<ArrayWrapper<short>{{key, {{9}, {8}, 7}}}>>::value);

int main() {
	Key<short> object{{12345678901LL}, {-7}, 42};
	Payload<short>* base = &object;
	return base->value == -7 && object.guard == 12345678901LL && object.tag == 42 ? 0 : 1;
}
