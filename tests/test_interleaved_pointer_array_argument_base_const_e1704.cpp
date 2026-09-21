// Adding const to the innermost base through a non-const ordered pointer chain
// is not a valid [conv.qual] conversion and must fail closed rather than be
// flattened into the legacy representation.
int (*(*value)[3])[4] = nullptr;

int consume(const int (*(*)[3])[4]);

int main() {
	consume(value);
	return 0;
}
