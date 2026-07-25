// Regression: typedef union Name { ... } Name must get implicit copy assignment
// (MSVC __m128i / wchar.h wmemchr pattern). Plain `union Name` already worked.
typedef union U {
	int x;
	char y[4];
} U;

int main() {
	U a{};
	U b{};
	a.x = 7;
	b = a;
	return b.x == 7 ? 0 : 1;
}
