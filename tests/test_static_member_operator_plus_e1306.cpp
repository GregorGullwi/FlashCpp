struct InvalidStaticOperator {
	static int operator+(int rhs) {
		return rhs;
	}
};

int main() {
	return 0;
}
