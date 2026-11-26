// Namespace-scope references to statically allocated objects keep their static
// relocation, and a local reference still binds the result of a runtime
// conversion function. The namespace-scope conversion form is diagnosed (see
// the _e1518 tests) because its referent is only known at run time.

struct MixedHolder {
	long long wide;
	int narrow;
	char tag;

	operator int&() {
		return narrow;
	}
};

int storage = 5;
const int& const_namespace_reference = storage;
int elements[3] = {11, 12, 13};
const int& const_element_reference = elements[2];

int main() {
	MixedHolder holder{4000000000LL, 17, 3};
	int& local_reference = holder;
	const int& local_const_reference = holder;
	local_reference += 16;
	if (local_const_reference != 33 || holder.narrow != 33) {
		return 1;
	}
	if (const_namespace_reference != 5) {
		return 2;
	}
	storage = 9;
	if (const_namespace_reference != 9) {
		return 3;
	}
	if (const_element_reference != 13) {
		return 4;
	}
	return 0;
}
