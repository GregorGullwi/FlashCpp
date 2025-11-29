// Test: Simple member initialization with implicit default constructor
// C++11 feature: Default member initializers

struct Test {
	int x = 10;
	int y = 20;
};

int main() {
	Test t;
	return t.x + t.y;  // Should return 30
}

