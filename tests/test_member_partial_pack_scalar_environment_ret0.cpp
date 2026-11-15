// A pack-expanded pattern argument and a scalar pattern argument can normalize
// to the same canonical parameter type. Their partial-pattern environments must
// still differ so one declaration's deleted constructors cannot replace the
// other's schema.

struct Host {
	template <class... Args> struct Item;

	template <class T, class... Rest> struct Item<T*, Rest...> {
		Item(int) = delete;
		Item(char);
	};

	template <class T, class U> struct Item<T*, U> {
		Item(char);
	};
};

static_assert(!__is_constructible(Host::Item<int*, char, long>, int));
static_assert(__is_constructible(Host::Item<int*, char, long>, char));

int main() {
	return 0;
}
