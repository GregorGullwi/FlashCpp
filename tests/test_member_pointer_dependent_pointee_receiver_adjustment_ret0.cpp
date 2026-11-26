// A dependent data-member pointee must keep its member-pointer owner during
// instantiation. When the owner is dropped, `object.*pointer` degrades to a
// plain pointer offset and reads or writes the wrong subobject for any owner
// that is not the primary base at offset zero.

struct Header {
	int first;
	int second;
};

struct Primary {
	long primary;
};

struct Pair {
	int low;
	int high;
};

struct Secondary {
	int flag;
	long long wide;
	double real;
	Pair pair;
};

struct SingleLayout : Primary {};

struct MultipleLayout : Header, Primary, Secondary {};

template <class Owner, class Pointee>
Pointee read_member(MultipleLayout& object, Pointee Owner::* pointer) {
	return object.*pointer;
}

// A local copy keeps the member pointer through declaration substitution. When
// the pointee substitution drops the owner, the local degrades to a plain
// pointer and the legacy access reads the wrong offset instead of diagnosing.
template <class Owner, class Pointee>
Pointee read_member_copy(MultipleLayout& object, Pointee Owner::* pointer) {
	auto copy = pointer;
	return object.*copy;
}

template <class Owner, class Pointee>
Pointee read_single(SingleLayout& object, Pointee Owner::* pointer) {
	return object.*pointer;
}

template <class Owner, class Pointee>
void write_member(MultipleLayout& object, Pointee Owner::* pointer, Pointee value) {
	object.*pointer = value;
}

template <class Pointee>
struct Reader {
	static Pointee read(MultipleLayout& object, Pointee Secondary::* pointer) {
		return object.*pointer;
	}
};

template <class Pointee>
using SecondaryMember = Pointee Secondary::*;

template <class Pointee>
struct MemberAliasOwner {
	using Member = Pointee Secondary::*;
};

int main() {
	SingleLayout single{};
	single.primary = 123;
	long Primary::* primary = &Primary::primary;
	if (read_single<Primary, long>(single, primary) != 123) {
		return 1;
	}

	MultipleLayout object{};
	object.first = 11;
	object.second = 22;
	object.primary = 33;
	object.flag = 44;
	object.wide = 55;
	object.real = 66.5;
	object.pair.low = 111;
	object.pair.high = 222;

	if (read_member<Primary, long>(object, primary) != 33) {
		return 2;
	}

	int Secondary::* flag = &Secondary::flag;
	if (read_member<Secondary, int>(object, flag) != 44) {
		return 3;
	}

	long long Secondary::* wide = &Secondary::wide;
	if (read_member<Secondary, long long>(object, wide) != 55) {
		return 4;
	}

	double Secondary::* real = &Secondary::real;
	if (read_member<Secondary, double>(object, real) != 66.5) {
		return 5;
	}

	Pair Secondary::* pair = &Secondary::pair;
	Pair pair_value = read_member<Secondary, Pair>(object, pair);
	if (pair_value.low != 111 || pair_value.high != 222) {
		return 6;
	}
	if (read_member_copy<Secondary, int>(object, flag) != 44) {
		return 7;
	}
	if (read_member_copy<Secondary, double>(object, real) != 66.5) {
		return 8;
	}

	write_member<Secondary, int>(object, flag, 77);
	if (object.flag != 77 || read_member<Secondary, int>(object, flag) != 77) {
		return 9;
	}
	write_member<Secondary, long long>(object, wide, 88);
	if (object.wide != 88 || read_member<Secondary, long long>(object, wide) != 88) {
		return 10;
	}
	write_member<Secondary, double>(object, real, 99.5);
	if (object.real != 99.5 || read_member<Secondary, double>(object, real) != 99.5) {
		return 11;
	}
	Pair replacement{333, 444};
	write_member<Secondary, Pair>(object, pair, replacement);
	Pair stored_pair = read_member<Secondary, Pair>(object, pair);
	if (stored_pair.low != 333 || stored_pair.high != 444) {
		return 12;
	}

	if (Reader<int>::read(object, flag) != 77) {
		return 13;
	}
	if (Reader<double>::read(object, real) != 99.5) {
		return 14;
	}
	if (Reader<long long>::read(object, wide) != 88) {
		return 15;
	}

	SecondaryMember<int> alias_flag = &Secondary::flag;
	if (object.*alias_flag != 77) {
		return 16;
	}
	if (Reader<int>::read(object, alias_flag) != 77) {
		return 17;
	}

	MemberAliasOwner<int>::Member member_alias = &Secondary::flag;
	if (object.*member_alias != 77) {
		return 18;
	}
	return 0;
}
