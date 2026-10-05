// A concept requirement built on zero-argument __is_constructible must be
// answered from the canonical DefaultConstructible fact in the lazy constraint
// evaluator instead of being left as an unclassified Unknown outcome. The
// constrained overload takes a pointer so a non-constructible type can be
// queried without constructing it.
struct Plain {
	int value;
	long tag;
};

struct DeletedDefault {
	DeletedDefault() = delete;
};

struct Abstract {
	virtual void method() = 0;
};

struct BaseNoDefault {
	BaseNoDefault(int) {}
};

struct DerivedBad : BaseNoDefault {
};

struct MemberNoDefault {
	MemberNoDefault(int) {}
};

struct HasBadMember {
	MemberNoDefault member;
};

template <class Type>
struct Box {
	Type value;
};

template <typename Type>
concept DefaultConstructible = __is_constructible(Type);

template <typename Type>
requires DefaultConstructible<Type>
int probe(Type*) {
	return 1;
}

int probe(...) {
	return 2;
}

int main() {
	Plain plain{};
	Box<int> box{};
	int scalar = 0;
	DeletedDefault* deleted = nullptr;
	Abstract* abstract = nullptr;
	DerivedBad* derived = nullptr;
	HasBadMember* has_bad = nullptr;
	int* pointer = &scalar;

	int mismatches = 0;
	if (probe(&plain) != 1) {
		mismatches |= 1;
	}
	if (probe(&box) != 1) {
		mismatches |= 2;
	}
	if (probe(deleted) != 2) {
		mismatches |= 4;
	}
	if (probe(abstract) != 2) {
		mismatches |= 8;
	}
	if (probe(derived) != 2) {
		mismatches |= 16;
	}
	if (probe(has_bad) != 2) {
		mismatches |= 32;
	}
	if (probe(pointer) != 1) {
		mismatches |= 64;
	}
	return mismatches == 0 ? 0 : 1;
}
