// Expected to fail: reading a default-initialized local scalar is not a core constant expression.
constexpr int readLocalScalar() {
	int value;
	return value;
}

static_assert(readLocalScalar() == 0);

int main() {
	return 0;
}
