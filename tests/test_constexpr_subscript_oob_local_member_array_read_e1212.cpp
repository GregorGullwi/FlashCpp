// Reduced regression for DiagnosticId::ConstantExpressionArrayIndexOutOfBounds (1212).
// C++20 [expr.const]/4: a local object's member array remains bounds-checked
// when the member is selected directly inside constexpr evaluation.
struct Holder {
	int data[4];
};

constexpr int readMemberArray() {
	Holder holder;
	return holder.data[9];
}

static_assert(readMemberArray() == 0);

int main() {
	return 0;
}
