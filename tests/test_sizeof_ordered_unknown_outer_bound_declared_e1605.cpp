// sizeof of a declared ordered object with an omitted outer bound reads the
// declaration's complete canonical type, so it rejects instead of reporting
// the size of its record-row pointer elements ([expr.sizeof]/2).
struct Wide {
	long long a;
	long long b;
};

Wide (*outer_wide[])[3];

int main() {
	return static_cast<int>(sizeof(outer_wide));
}
