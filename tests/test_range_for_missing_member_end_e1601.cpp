// A range-based for loop selected member range access but the type only
// provides begin(), not end(). The partial-member case must report the same
// RangeForBeginEndRequired owner as the fully missing case.

struct OnlyBegin {
	int* begin() {
		return nullptr;
	}
};

int main() {
	OnlyBegin range;
	for (int value : range) {
		(void)value;
	}
	return 0;
}
