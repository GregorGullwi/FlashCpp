// Hidden friend operator+ defined inside a class body.
// Per C++20 [class.friend]/7, this is a hidden friend — it should only be
// findable via ADL when at least one argument has the associated class type.
struct Widget {
	int value;
	friend int operator+(Widget a, Widget b) { return a.value + b.value; }
};
int main() {
	Widget a;
	a.value = 3;
	Widget b;
	b.value = 4;
	// ADL should find operator+ because a and b are of type Widget
	return (a + b) - 7;  // 3 + 4 - 7 == 0
}
