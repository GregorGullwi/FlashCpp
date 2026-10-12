// A braced initializer list with more elements than the array bound is
// ill-formed and must report the shared TooManyInitializers owner.

int main() {
	int values[2] = {1, 2, 3};
	return values[0];
}
