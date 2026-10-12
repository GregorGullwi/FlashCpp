// A scalar member cannot be brace-initialized with more than one value. This
// partial-brace form must report the shared TooManyInitializers owner.

struct Pair {
	int first;
	int second;
};

int main() {
	Pair value{{1, 2}, 3};
	return value.first;
}
