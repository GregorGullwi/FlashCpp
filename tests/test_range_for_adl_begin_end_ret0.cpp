// Test: range-based for with free-function begin()/end() found via ADL
// C++20 [stmt.ranged]/1: when the range type has no member begin/end,
// ADL is used to find free-function begin()/end().

namespace geom {

struct PointList {
	int data[4];
	int count;
};

int* begin(PointList& pl) { return &pl.data[0]; }
int* end(PointList& pl) { return &pl.data[pl.count]; }

} // namespace geom

int main() {
	geom::PointList pl{{10, 20, 30, 40}, 4};
	int sum = 0;
	for (int v : pl) {
		sum += v;
	}
	// 10+20+30+40 = 100
	return sum == 100 ? 0 : 1;
}
