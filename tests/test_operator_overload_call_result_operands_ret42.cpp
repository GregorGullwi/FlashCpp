struct Vec {
	int value;

	Vec operator+(const Vec& other) const {
		return Vec{value + other.value};
	}
};

Vec makeVec(int value) {
	return Vec{value};
}

int main() {
	Vec sum = makeVec(10) + makeVec(32);
	return sum.value;
}
