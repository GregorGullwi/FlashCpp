// C++20 [expr.shift]/1 requires integral or unscoped enum operands.
// Metadata-backed member lvalues must use the structured shift diagnostic.
struct Holder {
	int value;
};

int main() {
	Holder holder{8};
	double shift = 3.0;
	holder.value >>= shift;
	return holder.value;
}
