// [dcl.array]/1, [expr.sizeof]/2: an array of record elements with an omitted
// outer bound still has no object size. sizeof(items) must reject the declared
// unknown-bound array rather than report the record row extent.
struct Item {
	long long value;
	int tag;
};

Item items[][2];

int main() {
	return static_cast<int>(sizeof(items));
}
