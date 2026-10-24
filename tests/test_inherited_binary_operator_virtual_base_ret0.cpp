struct Base {
	int value;
	int operator+(int right) const { return value + right; }
};
struct Padding { long long guard; };
struct Derived : Padding, virtual Base {};

int main() {
	Derived object;
	object.guard = 9999;
	object.value = 38;
	return object + 4 == 42 ? 0 : 1;
}
