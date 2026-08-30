// Regression anchor for direct-initialization diagnostics:
// this remains a hard parse failure, and the direct compiler repro for this
// file should surface the nested qualified-call error instead of collapsing it
// to a generic direct-initialization wrapper.
int main() {
	int value(Missing::f());
	return value;
}
