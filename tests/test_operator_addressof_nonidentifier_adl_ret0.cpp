// C++20 [over.match.oper]: unary operator overload resolution applies to any
// class operand expression and includes non-member candidates found by ADL.

namespace sample {

struct MemberAddress {
	int value;

	int operator&() {
		return value + 1;
	}
};

struct FreeAddress {
	int value;
};

int operator&(FreeAddress& value) {
	value.value += 2;
	return value.value;
}

struct ConstAddress {
	int value;

	int operator&() {
		return value + 1;
	}

	int operator&() const {
		return value + 2;
	}
};

} // namespace sample

struct Holder {
	sample::MemberAddress member;
	sample::FreeAddress free;
};

int main() {
	Holder holder{{40}, {40}};
	if (&holder.member != 41) {
		return 1;
	}
	if (&holder.free != 42) {
		return 2;
	}
	if (holder.free.value != 42) {
		return 3;
	}
	sample::ConstAddress mutable_address{40};
	if (&mutable_address != 41) {
		return 4;
	}
	const sample::ConstAddress const_address{40};
	if (&const_address != 42) {
		return 5;
	}
	return 0;
}
