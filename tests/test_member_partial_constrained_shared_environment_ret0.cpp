// Concept-constrained member partials with identical argument patterns share a
// canonical parameter environment because constraints affect selection, not the
// normalized pattern. Their type records must stay separate so one partial's
// deleted constructors cannot replace another partial's schema.

template <class T>
concept Accept = true;

template <class T>
concept Reject = false;

struct Host {
	template <class T> struct Item;

	template <Accept T> struct Item<T*> {
		Item(int) = delete;
		Item(char);
	};

	template <Reject T> struct Item<T*> {
		Item(char);
	};
};

static_assert(!__is_constructible(Host::Item<int*>, int));
static_assert(__is_constructible(Host::Item<int*>, char));

int main() {
	return 0;
}
