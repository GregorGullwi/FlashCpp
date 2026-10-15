// A dependent noexcept specification that folds to a constant after
// instantiation must still be accepted; the structured rejection for
// non-constant operands must not fire for a valid specification.

template <typename T>
void guarded() noexcept(sizeof(T) == 4) {}

int main() {
	guarded<int>();
	return 0;
}
