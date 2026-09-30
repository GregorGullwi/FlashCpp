struct Payload {
	int field;
	int member() {
		return field;
	}
};

int choose(const bool& value) {
	return value ? 1 : 0;
}

long choose(...) {
	return 2;
}

int function(int value) {
	return value;
}

int values[3];

using DataMemberPointer = int Payload::*;
using MemberFunctionPointer = int (Payload::*)();

static_assert(__is_same(decltype(choose(static_cast<int*>(nullptr))), int));
static_assert(__is_same(decltype(choose(values)), int));
static_assert(__is_same(decltype(choose(&function)), int));
static_assert(__is_same(
	decltype(choose(static_cast<DataMemberPointer>(nullptr))), int));
static_assert(__is_same(
	decltype(choose(static_cast<MemberFunctionPointer>(nullptr))), int));

int main() {
	int value = 42;
	int* pointer = &value;
	return choose(pointer) == 1 ? 0 : 1;
}
