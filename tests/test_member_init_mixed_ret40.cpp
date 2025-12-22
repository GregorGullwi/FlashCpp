// Test: Mixed initialization - some with defaults, some without
// Members without defaults should be zero-initialized

struct Test {
	int x = 10;
	int y;      // No default, should be zero-initialized
	int z = 30;
};

int main() {
	Test t;
	return t.x + t.y + t.z;  // Should return 40 (10 + 0 + 30)
}

