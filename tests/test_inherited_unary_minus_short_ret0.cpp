int calls = 0;
int evaluations = 0;
struct Base {
	short value;
	int operator-() const { ++calls; return -value - 3; }
};
struct Derived : Base {};
Derived make() {
	++evaluations;
	Derived object;
	object.value = 11;
	return object;
}

int main() {
	Derived object;
	object.value = 7;
	int result = -object;
	if (result != -10 || calls != 1) return 1;
	if (-make() != -14 || evaluations != 1 || calls != 2) return 2;
	return 0;
}
