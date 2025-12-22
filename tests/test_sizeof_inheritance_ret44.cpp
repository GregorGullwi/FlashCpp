// Test sizeof() with struct inheritance

struct Base {
	int x;
	int y;
};

struct Derived : Base {
	int z;
};

struct MultiLevel : Derived {
	double w;
};

int main() {
	// Test sizeof with inheritance
	int size_base = sizeof(Base);           // Should be 8 (4 + 4)
	int size_derived = sizeof(Derived);     // Should be 12 (8 + 4)
	int size_multi = sizeof(MultiLevel);    // Should be 24 (12 + 4 padding + 8)
	
	// Sum: 8 + 12 + 24 = 44
	return size_base + size_derived + size_multi;
}
