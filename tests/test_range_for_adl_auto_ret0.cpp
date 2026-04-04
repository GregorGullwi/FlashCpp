// Test: range-based for with free-function begin()/end() using auto type deduction
// Also tests const-qualified range and mixed types.

namespace container {

struct IntBuf {
	int data[5];
	int len;
};

int* begin(IntBuf& b) { return &b.data[0]; }
int* end(IntBuf& b) { return &b.data[b.len]; }

const int* begin(const IntBuf& b) { return &b.data[0]; }
const int* end(const IntBuf& b) { return &b.data[b.len]; }

} // namespace container

int sum_buf(container::IntBuf& buf) {
	int total = 0;
	for (auto v : buf) {
		total += v;
	}
	return total;
}

int main() {
	container::IntBuf buf{{10, 20, 30, 40, 50}, 5};
	int s = sum_buf(buf);
	// 10+20+30+40+50 = 150
	return s == 150 ? 0 : 1;
}
