// A namespace-scope pointer initialized from a decayed array with a nonzero
// element offset stores the base symbol plus the element's byte addend. The
// previous lowering stored null because static relocations had no addend.
// Each native width and record stride checks that the addend scales by the
// pointee size rather than by one byte or one element index.

struct Wide {
	long long first;
	long long second;
};

int values[2] = {7, 9};
int* p = values + 1;
int* element = &values[1];

long long wides[2] = {11, 22};
long long* wide_pointer = wides + 1;

Wide items[2] = {{1, 2}, {3, 4}};
Wide* item_pointer = items + 1;

int main() {
	if (p != element) {
		return 1;
	}
	if (*p != 9) {
		return 2;
	}
	*p = 13;
	if (values[1] != 13) {
		return 3;
	}
	if (*wide_pointer != 22) {
		return 4;
	}
	*wide_pointer = 33;
	if (wides[1] != 33) {
		return 5;
	}
	if (item_pointer->first != 3 || item_pointer->second != 4) {
		return 6;
	}
	item_pointer->second = 44;
	if (items[1].second != 44) {
		return 7;
	}
	return 0;
}
