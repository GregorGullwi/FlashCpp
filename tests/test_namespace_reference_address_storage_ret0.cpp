// A namespace-scope reference object stores the address of the object it
// binds. The previous lowering stored the referenced value or zero, so reading
// through the reference saw a stale copy and writing through it never reached
// the original object. The reference forms below cover a plain scalar, an
// alias-of-alias, a const reference, an array element, a record member, and a
// namespace-qualified name. The controls keep the scalar pointer, whole-array
// decay, and local reference paths on their existing storage.

struct Widget {
	int first;
	int second;
};

namespace Namespace {
int qualified_value = 51;
int& qualified_reference = qualified_value;
}

int scalar = 11;
int& scalar_reference = scalar;
int& alias_reference = scalar_reference;
const int& const_scalar_reference = scalar;

int elements[3] = {21, 22, 23};
int& element_reference = elements[1];

Widget widget{31, 32};
int& member_reference = widget.second;
const int& const_member_reference = widget.second;

// A prvalue-bound rvalue reference materializes one static temporary object.
int&& rvalue_reference = 61;

int main() {
	if (scalar_reference != 11) {
		return 1;
	}
	scalar_reference = 12;
	if (scalar != 12) {
		return 2;
	}
	if (alias_reference != 12) {
		return 3;
	}
	alias_reference = 13;
	if (scalar != 13) {
		return 4;
	}
	if (const_scalar_reference != 13) {
		return 5;
	}
	scalar_reference += 5;
	if (scalar != 18) {
		return 6;
	}
	if (element_reference != 22) {
		return 7;
	}
	element_reference = 24;
	if (elements[1] != 24) {
		return 8;
	}
	if (member_reference != 32 || const_member_reference != 32) {
		return 9;
	}
	member_reference = 33;
	if (widget.second != 33 || const_member_reference != 33) {
		return 10;
	}
	if (Namespace::qualified_reference != 51) {
		return 11;
	}
	Namespace::qualified_reference = 52;
	if (Namespace::qualified_value != 52) {
		return 12;
	}
	if (rvalue_reference != 61) {
		return 13;
	}

	// Controls: namespace scalar pointer, whole-array decay, local reference.
	int* scalar_pointer = &scalar;
	if (*scalar_pointer != 18) {
		return 14;
	}
	int* whole = elements;
	if (whole != elements || *whole != 21) {
		return 15;
	}
	whole[2] = 25;
	if (elements[2] != 25) {
		return 16;
	}
	int local = 41;
	int& local_reference = local;
	local_reference = 42;
	if (local != 42) {
		return 17;
	}
	local_reference++;
	if (local != 43) {
		return 18;
	}
	scalar_reference++;
	if (scalar != 19) {
		return 19;
	}
	--alias_reference;
	if (scalar != 18) {
		return 20;
	}
	return 0;
}
