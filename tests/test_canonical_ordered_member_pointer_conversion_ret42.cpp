// Qualified static-member lookup materializes its published canonical member
// pointer as a non-projectable ordered declarator. These overloads exercise
// [conv.mem] from that TypeId, including [conv.fctptr] noexcept relaxation.
struct Base {
	int data;
	int run() noexcept;
};

struct Derived : Base {};

using BaseDataMemberPointer = int Base::*;
using DerivedDataMemberPointer = int Derived::*;
using BaseFunctionMemberPointer = int (Base::*)() noexcept;
using DerivedFunctionMemberPointer = int (Derived::*)();

struct Holder {
	static BaseDataMemberPointer data_member;
	static BaseFunctionMemberPointer function_member;
};

struct DataSelection {
	char value[3];
};

struct DataFallback {
	char value[5];
};

struct FunctionSelection {
	char value[7];
};

struct FunctionFallback {
	char value[9];
};

DataSelection select_data_member(DerivedDataMemberPointer);
DataFallback select_data_member(...);
FunctionSelection select_function_member(DerivedFunctionMemberPointer);
FunctionFallback select_function_member(...);

int main() {
	const bool data_member_converts = sizeof(decltype(select_data_member(Holder::data_member))) == sizeof(DataSelection);
	const bool function_member_converts = sizeof(decltype(select_function_member(Holder::function_member))) == sizeof(FunctionSelection);
	return data_member_converts && function_member_converts ? 42 : 1;
}
