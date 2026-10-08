struct Owner {
	int data;
	int run();
};

using DataMemberPointer = int Owner::*;
using FunctionMemberPointer = int (Owner::*)();

struct DataMemberSource {
	operator DataMemberPointer() const { return nullptr; }
};

struct FunctionMemberSource {
	operator FunctionMemberPointer() const { return nullptr; }
};

struct BoolSelection {
	char value;
};

struct EllipsisSelection {
	char value[2];
};

BoolSelection choose_data_member(bool);
EllipsisSelection choose_data_member(...);

BoolSelection choose_function_member(bool);
EllipsisSelection choose_function_member(...);

static_assert(__is_same(
	decltype(choose_data_member(DataMemberSource{})), BoolSelection));
static_assert(__is_same(
	decltype(choose_function_member(FunctionMemberSource{})), BoolSelection));

int main() {
	return 0;
}
