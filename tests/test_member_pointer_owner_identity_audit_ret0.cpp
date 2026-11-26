// A member-pointer owner is semantic identity. Every path that derives a type
// specifier from a pattern - alias chains, class-template members, deduced
// function-template parameters, and the trailing-return reparse - must keep
// that owner and the pointer level. Otherwise `object.*pointer` adjusts the
// receiver as if the owner were the primary base at offset zero and reads or
// writes the wrong subobject. The test reads and writes real members of int,
// long, double, and a small struct through two layouts: an owner at offset
// zero and a non-primary owner behind padding.

struct AuditPrefix {
	int before;
	int after;
};

struct AuditPacked {
	int low;
	int high;
};

struct AuditZeroBase {
	int code;
	long wide;
	double real;
	AuditPacked pair;
};

struct AuditSplitBase {
	int code;
	long wide;
	double real;
	AuditPacked pair;
};

struct AuditZeroLayout : AuditZeroBase {};

struct AuditSplitLayout : AuditPrefix, AuditSplitBase {};

// Alias chain: each alias re-projects the owner of the previous one.
template <class T>
using ZeroMember = T AuditZeroBase::*;

template <class T>
using ZeroMemberChain = ZeroMember<T>;

template <class T>
using SplitMember = T AuditSplitBase::*;

template <class T>
using SplitMemberChain = SplitMember<T>;

// The alias chain and the class-template member alias must denote the same
// member-pointer type as the direct spelling.
static_assert(__is_same(ZeroMemberChain<int>, int AuditZeroBase::*));
static_assert(__is_same(SplitMemberChain<long>, long AuditSplitBase::*));

// Member pointer produced by a class-template member alias and a class-template
// member function.
template <class T>
struct ZeroAccess {
	using Member = T AuditZeroBase::*;

	template <class U>
	static U AuditZeroBase::* address_of_pair() {
		return &AuditZeroBase::pair;
	}

	static T read(AuditZeroLayout& object, Member pointer) {
		return object.*pointer;
	}

	static void write(AuditZeroLayout& object, Member pointer, T value) {
		object.*pointer = value;
	}
};

template <class T>
struct SplitAccess {
	using Member = T AuditSplitBase::*;

	template <class U>
	static U AuditSplitBase::* address_of_pair() {
		return &AuditSplitBase::pair;
	}

	static T read(AuditSplitLayout& object, Member pointer) {
		return object.*pointer;
	}

	static void write(AuditSplitLayout& object, Member pointer, T value) {
		object.*pointer = value;
	}
};

// A class-template member alias carries the owner, including a floating-point
// pointee that is only used as a parameter or direct member access in this test.
static_assert(__is_same(ZeroAccess<AuditPacked>::Member, AuditPacked AuditZeroBase::*));
static_assert(__is_same(SplitAccess<double>::Member, double AuditSplitBase::*));

// Dependent member pointer passed through a deduced function-template
// parameter: the pointee is deduced from the call with no explicit arguments.
template <class T>
T read_deduced(AuditZeroLayout& object, T AuditZeroBase::* pointer) {
	return object.*pointer;
}

template <class T>
void write_deduced(AuditZeroLayout& object, T AuditZeroBase::* pointer, T value) {
	object.*pointer = value;
}

template <class T>
T read_deduced(AuditSplitLayout& object, T AuditSplitBase::* pointer) {
	return object.*pointer;
}

template <class T>
void write_deduced(AuditSplitLayout& object, T AuditSplitBase::* pointer, T value) {
	object.*pointer = value;
}

// A dependent member-pointer trailing return type. The reparse of `T Owner::*`
// must keep both the owner and the pointer level instead of projecting onto the
// substituted pointee. Floating-point pointees are exercised through parameters
// and direct member access; returning such a pointer by value hits the separate
// IR/codegen register-class gap documented in docs/KNOWN_ISSUES.md.
template <class T>
auto zero_identity(T AuditZeroBase::* pointer) -> T AuditZeroBase::* {
	return pointer;
}

template <class T>
T read_zero_trailing(AuditZeroLayout& object, T AuditZeroBase::* pointer) {
	return object.*zero_identity<T>(pointer);
}

template <class T>
auto split_identity(T AuditSplitBase::* pointer) -> T AuditSplitBase::* {
	return pointer;
}

template <class T>
T read_split_trailing(AuditSplitLayout& object, T AuditSplitBase::* pointer) {
	return object.*split_identity<T>(pointer);
}

template <class T>
T read_chain(AuditSplitLayout& object, SplitMemberChain<T> pointer) {
	return object.*pointer;
}

template <class T>
void write_chain(AuditSplitLayout& object, SplitMemberChain<T> pointer, T value) {
	object.*pointer = value;
}

int main() {
	AuditZeroLayout zero{};
	AuditSplitLayout split{};

	// Owner at offset zero, mixed member types through an alias chain.
	ZeroMemberChain<int> zero_code = &AuditZeroBase::code;
	ZeroMemberChain<long> zero_wide = &AuditZeroBase::wide;
	ZeroMemberChain<double> zero_real = &AuditZeroBase::real;
	ZeroMemberChain<AuditPacked> zero_pair = &AuditZeroBase::pair;

	zero.*zero_code = 101;
	zero.*zero_wide = 202;
	zero.*zero_real = 303.5;
	AuditPacked zero_value{404, 505};
	zero.*zero_pair = zero_value;

	if (zero.*zero_code != 101 || zero.*zero_wide != 202 || zero.*zero_real != 303.5) {
		return 1;
	}
	if ((zero.*zero_pair).low != 404 || (zero.*zero_pair).high != 505) {
		return 2;
	}

	// Non-primary owner behind padding, mixed member types through an alias chain.
	SplitMemberChain<int> split_code = &AuditSplitBase::code;
	SplitMemberChain<long> split_wide = &AuditSplitBase::wide;
	SplitMemberChain<double> split_real = &AuditSplitBase::real;
	SplitMemberChain<AuditPacked> split_pair = &AuditSplitBase::pair;

	split.before = 7;
	split.after = 8;
	split.*split_code = 111;
	split.*split_wide = 222;
	split.*split_real = 333.5;
	AuditPacked split_value{444, 555};
	split.*split_pair = split_value;

	if (split.before != 7 || split.after != 8) {
		return 3;
	}
	if (split.*split_code != 111 || split.*split_wide != 222 || split.*split_real != 333.5) {
		return 4;
	}
	if ((split.*split_pair).low != 444 || (split.*split_pair).high != 555) {
		return 5;
	}

	// The same owners produced by class-template members.
	ZeroAccess<int>::Member access_zero_code = &AuditZeroBase::code;
	ZeroAccess<long>::Member access_zero_wide = &AuditZeroBase::wide;
	ZeroAccess<double>::Member access_zero_real = &AuditZeroBase::real;
	ZeroAccess<AuditPacked>::Member access_zero_pair = ZeroAccess<AuditPacked>::address_of_pair<AuditPacked>();

	ZeroAccess<int>::write(zero, access_zero_code, 606);
	ZeroAccess<long>::write(zero, access_zero_wide, 707);
	ZeroAccess<double>::write(zero, access_zero_real, 808.5);
	AuditPacked access_zero_value{909, 1010};
	ZeroAccess<AuditPacked>::write(zero, access_zero_pair, access_zero_value);
	if (ZeroAccess<int>::read(zero, access_zero_code) != 606 ||
		ZeroAccess<long>::read(zero, access_zero_wide) != 707 ||
		ZeroAccess<double>::read(zero, access_zero_real) != 808.5) {
		return 6;
	}
	AuditPacked access_zero_read = ZeroAccess<AuditPacked>::read(zero, access_zero_pair);
	if (access_zero_read.low != 909 || access_zero_read.high != 1010) {
		return 7;
	}

	SplitAccess<int>::Member access_split_code = &AuditSplitBase::code;
	SplitAccess<long>::Member access_split_wide = &AuditSplitBase::wide;
	SplitAccess<double>::Member access_split_real = &AuditSplitBase::real;
	SplitAccess<AuditPacked>::Member access_split_pair = SplitAccess<AuditPacked>::address_of_pair<AuditPacked>();

	SplitAccess<int>::write(split, access_split_code, 1111);
	SplitAccess<long>::write(split, access_split_wide, 1212);
	SplitAccess<double>::write(split, access_split_real, 1313.5);
	AuditPacked access_split_value{1414, 1515};
	SplitAccess<AuditPacked>::write(split, access_split_pair, access_split_value);
	if (split.before != 7 || split.after != 8) {
		return 8;
	}
	if (SplitAccess<int>::read(split, access_split_code) != 1111 ||
		SplitAccess<long>::read(split, access_split_wide) != 1212 ||
		SplitAccess<double>::read(split, access_split_real) != 1313.5) {
		return 9;
	}
	AuditPacked access_split_read = SplitAccess<AuditPacked>::read(split, access_split_pair);
	if (access_split_read.low != 1414 || access_split_read.high != 1515) {
		return 10;
	}

	// Deduced function-template parameters: no explicit template arguments.
	write_deduced(zero, access_zero_code, 1616);
	write_deduced(zero, access_zero_wide, 1717L);
	write_deduced(zero, access_zero_real, 1818.5);
	AuditPacked deduced_zero_value{1919, 2020};
	write_deduced<AuditPacked>(zero, access_zero_pair, deduced_zero_value);
	if (read_deduced(zero, access_zero_code) != 1616 ||
		read_deduced(zero, access_zero_wide) != 1717 ||
		read_deduced(zero, access_zero_real) != 1818.5) {
		return 11;
	}
	AuditPacked deduced_zero_read = read_deduced(zero, access_zero_pair);
	if (deduced_zero_read.low != 1919 || deduced_zero_read.high != 2020) {
		return 12;
	}

	write_deduced(split, access_split_code, 2121);
	write_deduced(split, access_split_wide, 2222L);
	write_deduced(split, access_split_real, 2323.5);
	AuditPacked deduced_split_value{2424, 2525};
	write_deduced<AuditPacked>(split, access_split_pair, deduced_split_value);
	if (split.before != 7 || split.after != 8) {
		return 13;
	}
	if (read_deduced(split, access_split_code) != 2121 ||
		read_deduced(split, access_split_wide) != 2222 ||
		read_deduced(split, access_split_real) != 2323.5) {
		return 14;
	}
	AuditPacked deduced_split_read = read_deduced(split, access_split_pair);
	if (deduced_split_read.low != 2424 || deduced_split_read.high != 2525) {
		return 15;
	}

	// Dependent trailing return type, used directly as a member access operand.
	if (read_zero_trailing(zero, access_zero_code) != 1616 ||
		read_zero_trailing(zero, access_zero_wide) != 1717) {
		return 16;
	}
	AuditPacked trailing_zero_read = read_zero_trailing(zero, access_zero_pair);
	if (trailing_zero_read.low != 1919 || trailing_zero_read.high != 2020) {
		return 17;
	}

	if (read_split_trailing(split, access_split_code) != 2121 ||
		read_split_trailing(split, access_split_wide) != 2222) {
		return 18;
	}
	AuditPacked trailing_split_read = read_split_trailing(split, access_split_pair);
	if (trailing_split_read.low != 2424 || trailing_split_read.high != 2525) {
		return 19;
	}

	// Deduced trailing-return call: T is inferred from the member pointer.
	if (zero.*zero_identity(access_zero_code) != 1616 ||
		zero.*zero_identity(access_zero_wide) != 1717) {
		return 20;
	}
	if (split.*split_identity(access_split_code) != 2121 ||
		split.*split_identity(access_split_wide) != 2222) {
		return 21;
	}

	// The stored form must keep the owner too; a truncated projection would make
	// the member offset depend on the primary base position.
	int AuditZeroBase::* stored_zero_code = zero_identity<int>(access_zero_code);
	int AuditSplitBase::* stored_split_code = split_identity<int>(access_split_code);
	long AuditSplitBase::* stored_split_wide = split_identity<long>(access_split_wide);
	AuditPacked AuditSplitBase::* stored_split_pair = split_identity<AuditPacked>(access_split_pair);

	if (zero.*stored_zero_code != 1616) {
		return 22;
	}
	if (split.*stored_split_code != 2121 || split.*stored_split_wide != 2222) {
		return 23;
	}
	AuditPacked stored_split_read = split.*stored_split_pair;
	if (stored_split_read.low != 2424 || stored_split_read.high != 2525) {
		return 24;
	}

	// Alias-chain parameter in a function template with an explicit argument.
	write_chain<int>(split, split_code, 2626);
	write_chain<long>(split, split_wide, 2727);
	write_chain<double>(split, split_real, 2828.5);
	AuditPacked chain_value{2929, 3030};
	write_chain<AuditPacked>(split, split_pair, chain_value);
	if (split.before != 7 || split.after != 8) {
		return 26;
	}
	if (read_chain<int>(split, split_code) != 2626 || read_chain<long>(split, split_wide) != 2727 ||
		read_chain<double>(split, split_real) != 2828.5) {
		return 27;
	}
	AuditPacked chain_read = read_chain<AuditPacked>(split, split_pair);
	if (chain_read.low != 2929 || chain_read.high != 3030) {
		return 28;
	}

	return 0;
}
