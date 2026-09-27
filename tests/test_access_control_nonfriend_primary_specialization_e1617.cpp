// A friend declaration naming one specialization must not leak to other
// specializations instantiated from the same primary template. The primary
// pattern body is access-deferred, so this rejection must come from the
// concrete Container<double> instantiation.

template <class T>
struct Container;

class Secret {
	int value;
	friend struct Container<int>;

public:
	Secret(int initial) : value(initial) {}
};

template <class T>
struct Container {
	int read(Secret& secret) {
		return secret.value;
	}
};

int main() {
	Secret secret{42};
	Container<double> container;
	return container.read(secret);
}
