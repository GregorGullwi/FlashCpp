// C++20 [temp.arg.nontype] and [class.compare.default]: structural values are
// compared member by member, so a non-null object pointer member must contribute
// the object it points to. Two values that differ only in that target, or only in
// a neighbouring scalar member, must instantiate different specializations.
struct Node {
	int value;
};

Node first_node{1};
Node second_node{2};

struct Key {
	Node* pointer;
	int tag;
};

constexpr Key key_first{&first_node, 1};
constexpr Key key_second{&second_node, 1};
constexpr Key key_retagged{&first_node, 2};

template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };

template<Key Value> struct Holder { int marker; };

int main() {
	int result = 0;
	if (!Same<Holder<key_first>, Holder<key_first>>::value) {
		result |= 1;
	}
	if (Same<Holder<key_first>, Holder<key_second>>::value) {
		result |= 2;
	}
	if (Same<Holder<key_first>, Holder<key_retagged>>::value) {
		result |= 4;
	}
	return result;
}
