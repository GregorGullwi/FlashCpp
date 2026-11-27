// A pointer to data member is a 64-bit integer offset no matter what its
// pointee is, but its flat declarator projection spells it as the pointee
// category plus one pointer level. Comparisons and conversions that used that
// spelling converted the offset as a double/float/long double. Every check here
// first compares the member pointer with the address-of result, then reads,
// writes, or passes it through a real call, so a converted or narrowed offset
// changes the return code. Nonzero member offsets follow from the `prefix`
// field, and the int, float, and struct pointees are controls that must keep the
// same behaviour as the floating ones.

struct Inner {
	int code;
	float ratio;
};

struct B {
	int prefix;
	double wide;
	float narrow;
	Inner inner;
	int tail;
	long double extended;
};

struct D : B {
	int extra;
	double derived_wide;
};

struct Prefix {
	int value;
};

struct E : Prefix, B {
	int extra;
};

B b;
D d;
E e;

double read_through(const B& object, const double B::* pointer) {
	return object.*pointer;
}

float read_narrow_through(const B& object, const float B::* pointer) {
	return object.*pointer;
}

Inner read_inner_through(const B& object, const Inner B::* pointer) {
	return object.*pointer;
}

const double B::* const_wide_identity(const double B::* pointer) {
	return pointer;
}

const float B::* const_narrow_identity(const float B::* pointer) {
	return pointer;
}

const int B::* const_tail_identity(const int B::* pointer) {
	return pointer;
}

long double B::* extended_identity(long double B::* pointer) {
	return pointer;
}

int main() {
	b.prefix = 0x1234;
	b.wide = 5.25;
	b.narrow = 1.5f;
	b.inner.code = 7;
	b.inner.ratio = 8.5f;
	b.tail = 41;

	// Direct identity comparisons for a floating pointee.
	double B::* wide = &B::wide;
	if (!(wide == &B::wide)) {
		return 1;
	}
	if (wide != &B::wide) {
		return 2;
	}

	// Passing the address-of result straight into a const-pointee parameter must
	// not convert the offset before the callee reads the member.
	if (read_through(b, &B::wide) != 5.25) {
		return 3;
	}
	const double B::* const_wide = const_wide_identity(&B::wide);
	if (const_wide != &B::wide) {
		return 4;
	}
	if ((b.*const_wide) != 5.25) {
		return 5;
	}

	// float pointee control, including a write through the pointer.
	float B::* narrow = &B::narrow;
	if (narrow != &B::narrow) {
		return 6;
	}
	if (read_narrow_through(b, &B::narrow) != 1.5f) {
		return 7;
	}
	b.*narrow = -2.25f;
	if (b.narrow != -2.25f) {
		return 8;
	}
	const float B::* const_narrow = const_narrow_identity(&B::narrow);
	if ((b.*const_narrow) != -2.25f) {
		return 9;
	}

	// int pointee control at a nonzero offset.
	int B::* tail = &B::tail;
	if (tail != &B::tail) {
		return 10;
	}
	if ((b.*tail) != 41) {
		return 11;
	}
	b.*tail = 42;
	if (b.tail != 42) {
		return 12;
	}
	const int B::* const_tail = const_tail_identity(&B::tail);
	if (const_tail != &B::tail) {
		return 13;
	}
	if ((b.*const_tail) != 42) {
		return 14;
	}

	// struct pointee control: read and a member write through the pointer.
	Inner B::* inner = &B::inner;
	Inner read_inner = read_inner_through(b, &B::inner);
	if (read_inner.code != 7 || read_inner.ratio != 8.5f) {
		return 15;
	}
	(b.*inner).code = 9;
	if (b.inner.code != 9) {
		return 16;
	}

	// A base-owner member pointer keeps its integer offset when it converts to a
	// derived-owner member pointer with the same floating pointee. E inherits B as
	// a non-primary base, so the conversion has to add a nonzero base offset.
	d.derived_wide = 3.75;
	double D::* derived_wide = &D::derived_wide;
	if ((d.*derived_wide) != 3.75) {
		return 17;
	}
	double B::* wide_on_base = &B::wide;
	double D::* wide_on_derived = wide_on_base;
	d.wide = 5.25;
	if (wide_on_derived == nullptr) {
		return 18;
	}
	if ((d.*wide_on_derived) != 5.25) {
		return 19;
	}
	double E::* wide_on_non_primary = wide_on_base;
	e.wide = 6.5;
	e.prefix = 0x5678;
	if ((e.*wide_on_non_primary) != 6.5) {
		return 20;
	}

	// The null sentinel stays all-bits-one and distinct from a member at offset
	// zero or any real member.
	double B::* null_wide = nullptr;
	if (null_wide != nullptr) {
		return 21;
	}
	if (null_wide == &B::wide) {
		return 22;
	}
	if (!(null_wide == nullptr)) {
		return 23;
	}

	// A long double pointee round-trips its offset through a call even though
	// reading a long double value is still limited by the separate value
	// lowering gap documented in docs/KNOWN_ISSUES.md.
	long double B::* extended = extended_identity(&B::extended);
	if (extended != &B::extended) {
		return 24;
	}
	if (extended == &B::wide) {
		return 25;
	}

	// A conditional expression whose branches are member pointers (or a member
	// pointer and nullptr) must merge the offsets, not their pointees.
	bool choose_wide = true;
	double B::* conditional = choose_wide ? &B::wide : nullptr;
	if (conditional != &B::wide) {
		return 26;
	}
	if ((b.*conditional) != 5.25) {
		return 27;
	}
	double B::* conditional_copy = !choose_wide ? nullptr : conditional;
	if (conditional_copy != &B::wide) {
		return 28;
	}
	if ((b.*conditional_copy) != 5.25) {
		return 29;
	}

	return 0;
}
