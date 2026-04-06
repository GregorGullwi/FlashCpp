namespace math {
	int addOne(int x) {
		return x + 1;
	}

	struct Base {
		static int twice(int x) {
			return x * 2;
		}
	};
}

template <typename T>
struct Derived : math::Base {
	static int localTwice(int x) {
		return x * 2;
	}

	int run(T value) {
		return this->localTwice(static_cast<int>(value)) +
			   math::addOne(static_cast<int>(value)) +
			   Derived::localTwice(1);
	}
};

int main() {
	Derived<int> d;
	return d.run(13);
}
