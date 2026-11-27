// A pointer to data member is an integer-class scalar on both x64 ABIs no
// matter what its pointee is. The flat declarator projection spells one as the
// pointee category plus a single pointer level, so a return value classified by
// that category travels in XMM0 while the caller reads the integer return
// register. Every floating-pointee case here first compares the returned
// pointer value against a stored expectation, so a wrong register fails with a
// return code instead of dereferencing a garbage offset, and then reads and
// writes real members so a wrong owner-relative offset fails too. The int
// pointee and the member function pointer are controls, and the holder check
// covers a by-value aggregate whose first eightbyte is a member-pointer field.

struct MemberLayout {
	int prefix;
	double wide;
	float narrow;
	long double extended;
	int tail;

	int read_tail() {
		return tail;
	}
};

using MemberFunction = int (MemberLayout::*)();
using WideMember = double MemberLayout::*;
using WideIdentifier = WideMember (*)(WideMember);

double MemberLayout::* wide_identity(double MemberLayout::* pointer) {
	return pointer;
}

const double MemberLayout::* const_wide_identity(const double MemberLayout::* pointer) {
	return pointer;
}

float MemberLayout::* narrow_identity(float MemberLayout::* pointer) {
	return pointer;
}

int MemberLayout::* tail_identity(int MemberLayout::* pointer) {
	return pointer;
}

MemberFunction member_function_identity(MemberFunction pointer) {
	return pointer;
}

long double MemberLayout::* extended_identity(long double MemberLayout::* pointer) {
	return pointer;
}

WideMember wide_identity_via_pointer(WideMember pointer) {
	return pointer;
}

int member_pointers_match(double MemberLayout::* lhs, double MemberLayout::* rhs) {
	return lhs == rhs ? 1 : 0;
}

double read_wide_through(const MemberLayout& object, double MemberLayout::* pointer) {
	return object.*pointer;
}

struct MemberPointerHolder {
	double MemberLayout::* pointer;
	int tag;
};

MemberPointerHolder make_holder(double MemberLayout::* pointer, int tag) {
	MemberPointerHolder holder;
	holder.pointer = pointer;
	holder.tag = tag;
	return holder;
}

struct MemberFunctionHolder {
	MemberFunction function;
	int tag;
};

MemberFunctionHolder make_function_holder(MemberFunction function, int tag) {
	MemberFunctionHolder holder;
	holder.function = function;
	holder.tag = tag;
	return holder;
}

MemberLayout global_layout;

int main() {
	MemberLayout object;
	object.prefix = 0x1234567;
	object.wide = 1.5;
	object.narrow = 2.5f;
	object.tail = 7;

	double MemberLayout::* wide = wide_identity(&MemberLayout::wide);
	double MemberLayout::* expected_wide = &MemberLayout::wide;
	if (!member_pointers_match(wide, expected_wide)) {
		return 1;
	}
	if ((object.*wide) != 1.5) {
		return 2;
	}

	object.prefix = 0x2345678;
	float MemberLayout::* narrow = narrow_identity(&MemberLayout::narrow);
	float MemberLayout::* expected_narrow = &MemberLayout::narrow;
	if (!(narrow == expected_narrow)) {
		return 3;
	}
	if ((object.*narrow) != 2.5f) {
		return 4;
	}

	object.prefix = 0x3456789;
	int MemberLayout::* tail = tail_identity(&MemberLayout::tail);
	if ((object.*tail) != 7) {
		return 5;
	}

	MemberFunction tail_reader = member_function_identity(&MemberLayout::read_tail);
	if ((object.*tail_reader)() != 7) {
		return 6;
	}

	// A stored returned pointer must address the real member, and the write must
	// land in the object rather than in a projected copy.
	object.*wide = 7.25;
	if (object.wide != 7.25) {
		return 7;
	}
	if (read_wide_through(object, wide) != 7.25) {
		return 8;
	}
	object.*narrow = -3.5f;
	if (object.narrow != -3.5f) {
		return 9;
	}

	// Pointee cv must not change the integer-class classification. The local is
	// initialized directly so the const qualification conversion happens at the
	// declaration rather than on the address-of operand.
	const double MemberLayout::* const_local = &MemberLayout::wide;
	const double MemberLayout::* const_wide = const_wide_identity(const_local);
	if ((object.*const_wide) != 7.25) {
		return 10;
	}

	// The address-of result itself must compare equal to a stored pointer: the
	// comparison may not convert the offset through the floating pointee, and the
	// same address-of result passed straight to a const-pointee parameter must
	// reach the callee as one member pointer.
	if (!(wide == &MemberLayout::wide)) {
		return 21;
	}
	if (wide != &MemberLayout::wide) {
		return 22;
	}
	const double MemberLayout::* direct_const_wide = const_wide_identity(&MemberLayout::wide);
	if (direct_const_wide != &MemberLayout::wide) {
		return 23;
	}
	if ((object.*direct_const_wide) != 7.25) {
		return 24;
	}

	// A long double pointee keeps the same integer offset through a call.
	// Reading the long double value itself is limited by a separate value
	// lowering gap, so this compares the member-pointer identity only.
	long double MemberLayout::* extended = extended_identity(&MemberLayout::extended);
	if (extended != &MemberLayout::extended) {
		return 25;
	}
	if (extended == &MemberLayout::wide) {
		return 26;
	}

	// A call through a function pointer classifies the member-pointer result the
	// same way as a direct call.
	WideIdentifier indirect_identity = wide_identity_via_pointer;
	object.prefix = 0x56789ab;
	WideMember indirect_wide = indirect_identity(&MemberLayout::wide);
	if (!member_pointers_match(indirect_wide, wide)) {
		return 11;
	}
	if ((object.*indirect_wide) != 7.25) {
		return 12;
	}

	MemberPointerHolder holder = make_holder(wide, 33);
	if (holder.tag != 33) {
		return 13;
	}
	if (!member_pointers_match(holder.pointer, wide)) {
		return 14;
	}
	if ((object.*holder.pointer) != 7.25) {
		return 15;
	}

	MemberFunctionHolder function_holder = make_function_holder(tail_reader, 44);
	if (function_holder.tag != 44) {
		return 16;
	}
	if ((object.*function_holder.function)() != 7) {
		return 17;
	}

	// A global object receiver must use the object's storage, not a partially
	// copied temporary, exactly like the original floating-pointee repro.
	global_layout.prefix = 0x456789a;
	global_layout.wide = -1.25;
	global_layout.tail = 44;
	double MemberLayout::* global_wide = wide_identity(&MemberLayout::wide);
	if ((global_layout.*global_wide) != -1.25) {
		return 18;
	}
	global_layout.*global_wide = -2.5;
	if (global_layout.wide != -2.5) {
		return 19;
	}
	if (global_layout.tail != 44) {
		return 20;
	}

	return 0;
}
