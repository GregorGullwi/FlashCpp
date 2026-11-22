// A pointer to an array is a scalar pointer object and does not decay
// ([conv.array]/1), so int (*)[3] cannot become int*. The canonical structural
// plan owns this no-match for a supported pointer/array pair.
int (*row)[3] = nullptr;

int consume(int* element);

int main() {
	return consume(row);
}
