// Top-level partials with non-type pattern arguments publish a canonical
// environment. The selected pattern must supply its own deleted constructor
// schema.

template <class T, int N> struct Item;

template <class T> struct Item<T*, 1> {
	Item(int) = delete;
	Item(char);
};

static_assert(!__is_constructible(Item<int*, 1>, int));
static_assert(__is_constructible(Item<int*, 1>, char));

int main() {
	return 0;
}
