// A member pointer produced by address-of or static_cast must still select the
// member's own type for `obj.*pointer` and for sizeof.

struct S {
	int value;
	double d;
};

int main() {
	S object{1, 2.5};
	if (sizeof(object.*(&S::value)) != sizeof(int)) {
		return 1;
	}
	if (object.*(&S::d) != 2.5) {
		return 2;
	}
	if (object.*(static_cast<double S::*>(&S::d)) != 2.5) {
		return 3;
	}
	return 0;
}
