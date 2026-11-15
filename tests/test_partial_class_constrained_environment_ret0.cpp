// Top-level concept-constrained partials publish canonical environments. The
// selected partial must supply its own deleted constructor schema.

template <class T>
concept Always = true;

template <class T>
concept Never = false;

template <class T, class U> struct Item;

template <Always T> struct Item<T*, int> {
	Item(int) = delete;
	Item(char);
};

template <Never T> struct Item<T*, int> {
	Item(char);
};

static_assert(!__is_constructible(Item<int*, int>, int));
static_assert(__is_constructible(Item<int*, int>, char));

int main() {
	return 0;
}
