// A flat alias resolution must keep per-level pointer cv. Rebuilding a pointer
// chain from a depth count turned "using QualifiedChain = const int* const*"
// into "const int**", so the valid [conv.qual]/3 conversion from "int**" to
// "const int* const*" was rejected as if the inner const had been dropped.
// The cast must stay accepted and the resulting chain must address the value.

using QualifiedChain = const int* const*;

int main() {
	int value = 21;
	int* pointer = &value;
	int** plain = &pointer;
	QualifiedChain qualified = static_cast<QualifiedChain>(plain);
	if (**qualified != 21) {
		return 1;
	}
	return 0;
}
