// Class templates with a trailing parameter pack may be instantiated with
// only the non-pack arguments. The instantiation context must record the
// empty pack rather than rejecting a names/args size mismatch.

template <class Head, class... Tail>
struct PackHolder {
	Head head;
	static constexpr int tail_count = sizeof...(Tail);
};

struct Tiny {
	char byte;
};

struct Wide {
	int a;
	int b;
};

int main() {
	PackHolder<char> chars{};
	PackHolder<Tiny> tinies{};
	PackHolder<Wide> wides{};
	chars.head = 3;
	tinies.head.byte = 4;
	wides.head.a = 5;
	return (PackHolder<char>::tail_count == 0 &&
			PackHolder<Tiny>::tail_count == 0 &&
			PackHolder<Wide>::tail_count == 0 &&
			chars.head + tinies.head.byte + wides.head.a == 12)
		? 0
		: 1;
}
