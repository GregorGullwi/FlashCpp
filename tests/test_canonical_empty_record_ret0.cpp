// Complete empty records have a non-zero object size even when their layout
// cursor is zero. Publishing that normalized size must not abort compilation.
struct EmptyRecord {};

int main() {
	EmptyRecord value;
	(void)value;
	return 0;
}
