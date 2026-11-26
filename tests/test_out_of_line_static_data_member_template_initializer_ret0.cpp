// An out-of-line class-template static data member may carry an initializer and
// an array declarator. Each instantiation owns distinct storage, and the
// initializer is evaluated against that instantiation's own template arguments
// rather than a single shared definition.

template <typename T>
struct S {
	static int data;
	static int values[3];
};

template <typename T>
int S<T>::data = sizeof(T);
template <typename T>
int S<T>::values[3] = {sizeof(T), sizeof(T) + 1, sizeof(T) + 2};

int main() {
	if (S<int>::data != 4 || S<long long>::data != 8) {
		return 1;
	}
	if (S<int>::values[0] != 4 || S<int>::values[2] != 6) {
		return 2;
	}
	if (S<long long>::values[0] != 8 || S<long long>::values[2] != 10) {
		return 3;
	}

	S<int>::data = 40;
	S<long long>::data = 80;
	S<int>::values[1] = 41;
	S<long long>::values[1] = 81;
	if (S<int>::data != 40 || S<long long>::data != 80) {
		return 4;
	}
	if (S<int>::values[1] != 41 || S<long long>::values[1] != 81) {
		return 5;
	}
	return 0;
}
