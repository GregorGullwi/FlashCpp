// Regression: sema's qualified call-target recovery must honor the preserved
// owner-template-id metadata for nested owners inside the current
// instantiation, rather than falling through to a global same-spelled owner.

template <class T>
struct Base {
	template <class U>
	static int pick() {
		return 100 + static_cast<int>(sizeof(U));
	}
};

template <class T>
struct Outer {
	template <class U>
	struct Base {
		template <class V>
		static int pick() {
			return 38 + static_cast<int>(sizeof(V));
		}
	};

	static int run() {
		return Base<T>::template pick<int>();
	}
};

int main() {
	if (Outer<int>::run() != 42) {
		return 1;
	}

	if (Outer<char>::run() != 42) {
		return 2;
	}

	if (Base<int>::template pick<char>() != 101) {
		return 3;
	}

	return 0;
}
