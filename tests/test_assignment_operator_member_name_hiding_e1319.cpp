// A derived assignment operator name hides base assignment operators unless a
// using-declaration explicitly restores the nominated base overloads.
struct NonConvertible {
	NonConvertible() = delete;
};

struct Base {
	Base& operator=(int) { return *this; }
};

struct Derived : Base {
	Derived& operator=(NonConvertible) { return *this; }
};

int main() {
	Derived value;
	value = 1;
}
