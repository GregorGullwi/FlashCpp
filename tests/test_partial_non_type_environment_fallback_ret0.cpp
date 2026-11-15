// Top-level partials with non-type pattern arguments do not publish a canonical
// environment yet. The synthesized-name fallback must still select the right
// pattern record and preserve its deleted constructor schema.

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
