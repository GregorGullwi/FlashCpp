// The same rejection must apply to assignment, not only to initialization.
struct C {
	int f() const;
};

int main() {
	int* (C::*p)() const = nullptr;
	p = &C::f;
	return 0;
}
