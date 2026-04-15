// Regression test: inherited operator* returning by value (int, not int&).
struct ValDerefBase {
	int* ptr;
	int operator*() { return *ptr; }
};

struct ValIter : ValDerefBase {};

int main() {
	int value = 7;
	ValIter it;
	it.ptr = &value;
	int x = *it;
	return x - 7;
}
