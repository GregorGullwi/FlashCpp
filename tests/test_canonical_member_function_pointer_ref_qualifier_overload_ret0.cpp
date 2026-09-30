struct QualifiedOwner {
	int lvalue(int value) &;
	int rvalue(int value) &&;
	int overloaded(int value) &;
	int overloaded(int value) &&;
};

using LvalueMember = int (QualifiedOwner::*)(int) &;
using RvalueMember = int (QualifiedOwner::*)(int) &&;

struct LvalueChoice {
	char marker[3];
};

struct RvalueChoice {
	char marker[5];
};

struct EllipsisFallback {
	char marker[7];
};

LvalueChoice select_lvalue_member(LvalueMember);
EllipsisFallback select_lvalue_member(...);
RvalueChoice select_rvalue_member(RvalueMember);
EllipsisFallback select_rvalue_member(...);

static_assert(__is_same(
	LvalueMember, decltype(&QualifiedOwner::lvalue)));
static_assert(__is_same(
	RvalueMember, decltype(&QualifiedOwner::rvalue)));
static_assert(__is_same(
	decltype(select_lvalue_member(&QualifiedOwner::lvalue)),
	LvalueChoice));
static_assert(__is_same(
	decltype(select_rvalue_member(&QualifiedOwner::rvalue)),
	RvalueChoice));
static_assert(__is_same(
	decltype(select_lvalue_member(&QualifiedOwner::overloaded)),
	LvalueChoice));
static_assert(__is_same(
	decltype(select_rvalue_member(&QualifiedOwner::overloaded)),
	RvalueChoice));

int main() {
	return sizeof(decltype(
		select_lvalue_member(&QualifiedOwner::lvalue))) ==
			sizeof(LvalueChoice) &&
		sizeof(decltype(
			select_rvalue_member(&QualifiedOwner::rvalue))) ==
			sizeof(RvalueChoice) &&
		sizeof(decltype(
			select_lvalue_member(&QualifiedOwner::overloaded))) ==
			sizeof(LvalueChoice) &&
		sizeof(decltype(
			select_rvalue_member(&QualifiedOwner::overloaded))) ==
			sizeof(RvalueChoice)
		? 0
		: 1;
}
