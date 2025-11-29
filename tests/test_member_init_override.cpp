// Test: Constructor initializer list overrides default member initializers
// Explicit initializer should take precedence over default

struct Test {
	int x = 10;
	int y = 20;
	
	Test() : x(100) {}  // Override x, y should use default
};

int main() {
	Test t;
	return t.x + t.y;  // Should return 120 (100 + 20)
}

