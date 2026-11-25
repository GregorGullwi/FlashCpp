// Controls for the namespace-scope relocation addend work: a scalar pointer,
// whole-array decay, a local aggregate with a pointer member, and local
// decayed pointer arithmetic must all keep their answers.

int x = 17;
int values[2] = {3, 4};
int* scalar = &x;
int* whole = values;

struct S {
	int* p;
	int tag;
};

int main() {
	if (scalar != &x || *scalar != 17) {
		return 1;
	}
	if (whole != values || *whole != 3) {
		return 2;
	}
	int local_x = 5;
	S local{&local_x, 6};
	if (local.p != &local_x || local.tag != 6) {
		return 3;
	}
	*local.p = 11;
	if (local_x != 11) {
		return 4;
	}
	int local_values[2] = {8, 9};
	int* local_pointer = local_values + 1;
	if (*local_pointer != 9) {
		return 5;
	}
	return 0;
}
