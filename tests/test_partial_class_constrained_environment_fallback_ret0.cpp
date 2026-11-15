// Top-level concept-constrained partials still use the synthesized-name fallback
// because class-pattern canonical import does not publish constrained parameter
// environments yet. The fallback must keep their deleted constructor schemas
// separate and select the constrained partial.

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
