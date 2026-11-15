// Member partials with identical argument patterns and different requires
// clauses must keep separate pattern records so the selected partial supplies
// its own deleted constructors.

template <class T>
concept Always = true;

template <class T>
concept Never = false;

struct Host {
	template <class T> struct Item;

	template <class T> requires Always<T> struct Item<T*> {
		Item(int) = delete;
		Item(char);
	};

	template <class T> requires Never<T> struct Item<T*> {
		Item(char);
	};
};

static_assert(!__is_constructible(Host::Item<int*>, int));
static_assert(__is_constructible(Host::Item<int*>, char));

int main() {
	return 0;
}
