// __is_same in a lazy concept must compare a class-template specialization
// through its published canonical identity, not defer the requirement to an
// Unknown outcome. The constrained overload takes a pointer so the operand is
// queried without constructing it.
template <class Type>
struct Box {
	Type value;
};

template <typename Type>
concept SameAsBoxInt = __is_same(Type, Box<int>);

template <typename Type>
requires SameAsBoxInt<Type>
int probe(Type*) {
	return 1;
}

int probe(...) {
	return 2;
}

int main() {
	Box<int>* box_int = nullptr;
	Box<char>* box_char = nullptr;
	int scalar = 0;

	int mismatches = 0;
	if (probe(box_int) != 1) {
		mismatches |= 1;
	}
	if (probe(box_char) != 2) {
		mismatches |= 2;
	}
	if (probe(&scalar) != 2) {
		mismatches |= 4;
	}
	return mismatches == 0 ? 0 : 1;
}
