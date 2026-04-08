struct Vec {
	int value;

	Vec operator+(const Vec& other) const {
		return Vec{value + other.value};
	}
};

struct Wrapper {
	Vec vec;
};

int main() {
	Wrapper lhs{{10}};
	Wrapper rhs{{32}};

	Vec sum = lhs.vec + rhs.vec;
	return sum.value;
}
