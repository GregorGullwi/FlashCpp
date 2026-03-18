// Hidden friend operators defined inside a class body.
// Per C++20 [class.friend]/7, these are hidden friends — they should only be
// findable via ADL when at least one argument has the associated class type.
//
// Tests both operator+ and operator== as hidden friends.
//
// Return value is 0 on success.
struct Widget {
	int value;
	friend int operator+(Widget a, Widget b) { return a.value + b.value; }
	friend bool operator==(Widget a, Widget b) { return a.value == b.value; }
};
int main() {
	Widget a;
	a.value = 3;
	Widget b;
	b.value = 4;
	// ADL should find operator+ because a and b are of type Widget
	int sum = a + b;  // 3 + 4 == 7
	if (sum != 7) return 1;
	// ADL should find operator== because a and b are of type Widget
	Widget c;
	c.value = 3;
	if (!(a == c)) return 2;  // a.value == c.value == 3
	if (a == b) return 3;     // a.value != b.value
	return 0;
}
