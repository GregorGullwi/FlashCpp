// Test sizeof() with structs

struct Point {
	int x;
	int y;
};

struct LargeStruct {
	int a;
	int b;
	int c;
	double d;
};

int main() {
	// Test sizeof with primitive types
	int size_int = sizeof(int);        // Should be 4
	int size_double = sizeof(double);  // Should be 8
	
	// Test sizeof with struct types
	int size_point = sizeof(Point);         // Should be 8 (4 + 4)
	int size_large = sizeof(LargeStruct);   // Should be 24 (4 + 4 + 4 + 8 + padding)
	
	// Sum all sizes: 4 + 8 + 8 + 24 = 44
	return size_int + size_double + size_point + size_large;
}
