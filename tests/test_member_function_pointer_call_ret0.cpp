// A call through a materialized member-function pointer passes the object as
// this and the explicit arguments to the selected member, for both .* and ->*.

struct Counter {
	int value;

	int add(int amount) {
		return value + amount;
	}

	int addConst(int amount) const {
		return value + amount + 1;
	}
};

using AddPointer = int (Counter::*)(int);
using ConstAddPointer = int (Counter::*)(int) const;

int main() {
	Counter counter{40};
	Counter* pointer = &counter;
	AddPointer add = &Counter::add;
	if ((counter.*add)(2) != 42) {
		return 1;
	}
	if ((pointer->*add)(2) != 42) {
		return 2;
	}
	add = &Counter::add;
	if ((counter.*add)(1) != 41) {
		return 3;
	}
	const Counter const_counter{40};
	ConstAddPointer const_add = &Counter::addConst;
	if ((const_counter.*const_add)(1) != 42) {
		return 4;
	}
	return 0;
}
