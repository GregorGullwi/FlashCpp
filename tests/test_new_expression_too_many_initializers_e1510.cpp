// A new-expression with more braced initializers than the aggregate has members
// reaches the aggregate new-initializer path and must report the same
// TooManyInitializers owner.

struct Pair {
	int first;
	int second;
};

int main() {
	Pair* pointer = new Pair{1, 2, 3};
	delete pointer;
	return 0;
}
