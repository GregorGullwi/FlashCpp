// T7/T22/T25: Test type aliases in various positions:
//   - function returning a type alias
//   - struct member access through alias
//   - alias to struct

using MyInt = int;
using MyLong = long long;

struct Point {
	int x;
	int y;
};
using Pt = Point;

// Function returning a type alias
MyInt makeInt(int v) { return v; }
MyLong makeLong(long long v) { return v; }

// Alias to struct: member access
Pt makePoint(int x, int y) { return Pt{x, y}; }
MyInt sumPoint(Pt p) { return p.x + p.y; }

// Type alias used for local variable
int useAlias() {
	MyInt a = 10;
	MyLong b = 20;
	Pt p = makePoint(3, 7);
	return (a + static_cast<int>(b) + sumPoint(p) == 40) ? 0 : 1;
}

// Template with type alias member type
template <typename T>
struct Wrapper {
	using value_type = T;
	T v;
	T get() const { return v; }
};

using WrapInt = Wrapper<int>;
using WrapLong = Wrapper<long long>;

int main() {
	if (makeInt(42) != 42)
		return 1;
	if (makeLong(100) != 100)
		return 2;
	if (useAlias() != 0)
		return 3;

	WrapInt wi{55};
	if (wi.get() != 55)
		return 4;
	WrapLong wl{77};
	if (wl.get() != 77)
		return 5;

	// Alias to struct member access
	Pt p = makePoint(15, 27);
	if (p.x != 15)
		return 6;
	if (p.y != 27)
		return 7;

	return 0;
}
