// A class-template static data member may be defined out of line without an
// initializer, including an array declarator. Each instantiation owns distinct
// storage, and the element type may itself be the template parameter. Values are
// written and read through both scalar and array members so a definition that
// merely parses but never attaches storage fails.

template <typename T>
struct Holder {
	static int data;
	static int values[3];
};

template <typename T>
int Holder<T>::data;
template <typename T>
int Holder<T>::values[3];

template <typename T>
struct Typed {
	static T data;
	static T values[3];
};

template <typename T>
T Typed<T>::data;
template <typename T>
T Typed<T>::values[3];

int main() {
	Holder<int>::data = 11;
	Holder<long long>::data = 22;
	Holder<int>::values[1] = 33;
	Holder<long long>::values[2] = 44;

	Typed<int>::data = 55;
	Typed<long long>::data = 66;
	Typed<int>::values[0] = 77;
	Typed<long long>::values[1] = 88;

	int* local_decayed = Holder<int>::values;
	local_decayed[0] = 99;
	if (Holder<int>::values[0] != 99 || &local_decayed[0] != &Holder<int>::values[0]) {
		return 1;
	}

	if (Holder<int>::data != 11) {
		return 2;
	}
	if (Holder<long long>::data != 22) {
		return 2;
	}
	if (Holder<int>::values[1] != 33) {
		return 3;
	}
	if (Holder<long long>::values[2] != 44) {
		return 4;
	}
	if (Typed<int>::data != 55) {
		return 5;
	}
	if (Typed<long long>::data != 66) {
		return 6;
	}
	if (Typed<int>::values[0] != 77) {
		return 7;
	}
	if (Typed<long long>::values[1] != 88) {
		return 8;
	}
	return 0;
}
