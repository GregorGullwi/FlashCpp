struct Base {
	int base_value;
};

struct Derived : Base {
	int derived_value;
};

using BaseMemberPointer = int Base::*;
using DerivedMemberPointer = int Derived::*;
using BaseMemberFunctionPointer = int (Base::*)();
using DerivedMemberFunctionPointer = int (Derived::*)();

struct MemberPointerSource {
	operator BaseMemberPointer() const {
		return &Base::base_value;
	}
};

struct MemberFunctionPointerSource {
	operator BaseMemberFunctionPointer() const;
};

struct BaseMemberFunctionSelection {
	char marker[11];
};

struct DerivedMemberFunctionSelection {
	char marker[13];
};

BaseMemberFunctionSelection select_member_function(
	BaseMemberFunctionPointer member);
DerivedMemberFunctionSelection select_member_function(
	DerivedMemberFunctionPointer member);

static_assert(__is_same(
	decltype(select_member_function(MemberFunctionPointerSource{})),
	BaseMemberFunctionSelection));

struct BaseMemberSelection {
	int marker;
};

struct DerivedMemberSelection {
	int marker;
};

struct EllipsisSelection {
	int marker;
};

BaseMemberSelection select_member_pointer(BaseMemberPointer) {
	return {31};
}

DerivedMemberSelection select_member_pointer(DerivedMemberPointer) {
	return {73};
}

EllipsisSelection select_member_pointer(...) {
	return {99};
}

static_assert(__is_same(
	decltype(select_member_pointer(MemberPointerSource{})),
	BaseMemberSelection));

int main() {
	return sizeof(decltype(select_member_pointer(MemberPointerSource{}))) ==
		sizeof(BaseMemberSelection) &&
		sizeof(decltype(select_member_function(MemberFunctionPointerSource{}))) ==
			sizeof(BaseMemberFunctionSelection) &&
		select_member_pointer(MemberPointerSource{}).marker == 31
		? 0
		: 1;
}
