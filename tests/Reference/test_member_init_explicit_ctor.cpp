// Test: Member initialization with explicit empty constructor
// Default initializers should be used when constructor doesn't override them

struct Test {
	int x = 10;
	int y = 20;
	
	Test() {}  // Empty constructor, should use default initializers
};

int main() {
	Test t;
	return t.x + t.y;  // Should return 30
}

