// A positional braced initializer list with more elements than the aggregate
// has members must report TooManyInitializers, the same owner as the array case.

struct Pair {
	int first;
	int second;
};

int main() {
	Pair value{1, 2, 3};
	return value.first;
}
