// A friend declaration that names a concrete specialization grants access to
// that specialization's instantiated member bodies, whether the body comes
// from the primary template or a partial specialization pattern. Access inside
// the uninstantiated pattern is deferred and re-checked against the exact
// specialization identity, so the pattern deferral never grants access to a
// non-friend specialization.

template <typename T>
struct Container;

class Secret {
	int value;
	friend struct Container<int>;
	friend struct Container<int*>;

public:
	Secret(int initial) : value(initial) {}
};

template <typename T>
struct Container {
	T data;
	int read(Secret& secret) { return secret.value; }
};

template <typename T>
struct Container<T*> {
	T* data;
	int read(Secret& secret) { return secret.value + 10; }
};

int main() {
	int local = 0;
	Secret secret{42};

	Container<int> by_value{1};
	if (by_value.read(secret) != 42) {
		return 1;
	}

	Container<int*> by_pointer{&local};
	if (by_pointer.read(secret) != 52) {
		return 2;
	}

	return 0;
}
