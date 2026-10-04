struct Prefix {
	int prefix_value;
};

struct Base {
	int base_value;
};

struct Derived : Prefix, Base {
	int derived_value;
};

using BaseMemberPointer = int Base::*;
using DerivedMemberPointer = int Derived::*;

struct MemberPointerSource {
	bool is_null;

	operator BaseMemberPointer() const {
		BaseMemberPointer member = nullptr;
		if (!is_null) {
			member = &Base::base_value;
		}
		return member;
	}
};

int read_member(Derived& object, DerivedMemberPointer member) {
	return object.*member;
}

bool is_null_member(DerivedMemberPointer member) {
	return !member;
}

bool is_null_base_member(BaseMemberPointer member) {
	return !member;
}

int main() {
	Derived object{};
	object.base_value = 73;
	object.derived_value = 19;
	if (read_member(object, &Derived::base_value) != 73) {
		return 4;
	}
	const int member_result =
		read_member(object, MemberPointerSource{false});
	if (member_result != 73) {
		return 2;
	}
	if (!is_null_base_member(MemberPointerSource{true})) {
		return 3;
	}
	return is_null_member(MemberPointerSource{true}) ? 0 : 5;
}
