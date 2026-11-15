// Concept arguments that name different record types must produce different
// partial-pattern environments. Diagnostic type spellings such as "struct" are
// not type identity, so distinct record arguments must not collapse.

struct Allow {};
struct Deny {};

template <class T, class Tag>
concept Accepts = __is_same(Tag, Allow);

struct Host {
	template <class T> struct Item;

	template <Accepts<Allow> T> struct Item<T*> {
		Item(int) = delete;
		Item(char);
	};

	template <Accepts<Deny> T> struct Item<T*> {
		Item(char);
	};
};

static_assert(!__is_constructible(Host::Item<int*>, int));
static_assert(__is_constructible(Host::Item<int*>, char));

int main() {
	return 0;
}
