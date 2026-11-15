struct A {
	enum class E {
		value = 1,
	};
};

struct B {
	enum class E {
		value = 2,
	};
};

using Alias = A;

struct D : Alias, B {
	int ambiguous() {
		return static_cast<int>(E::value);
	}
};

int main() {
	D d;
	return d.ambiguous();
}
