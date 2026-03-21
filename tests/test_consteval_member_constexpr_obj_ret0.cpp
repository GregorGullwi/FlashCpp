// Test consteval member function called on a constexpr object that accesses 'this' state.
// C++20 [dcl.consteval]: every call to a consteval function must be a constant expression.
// When the object is constexpr, the evaluator must use the member-function path
// so it can resolve 'this' member bindings correctly.

struct Point {
	int x, y;
	consteval int sum() const { return x + y; }
	consteval int scaled_sum(int factor) const { return (x + y) * factor; }
};

struct Counter {
	int value;
	consteval int doubled() const { return value * 2; }
};

int main() {
	constexpr Point p{3, 4};
	constexpr int s = p.sum();        // 3 + 4 == 7
	constexpr int ss = p.scaled_sum(2); // (3 + 4) * 2 == 14

	constexpr Counter c{5};
	constexpr int d = c.doubled();   // 5 * 2 == 10

	// s + ss + d == 7 + 14 + 10 == 31, return 31 - 31 == 0
	return s + ss + d - 31;
}
