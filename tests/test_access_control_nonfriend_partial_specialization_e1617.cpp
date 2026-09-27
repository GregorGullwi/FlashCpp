template <class T>
struct Container;

class Secret {
	int value;
	friend struct Container<int*>;

public:
	Secret(int initial) : value(initial) {}
};

template <class T>
struct Container<T*> {
	int read(Secret& secret) {
		return secret.value;
	}
};

int main() {
	Secret secret{42};
	Container<double*> container;
	return container.read(secret);
}
