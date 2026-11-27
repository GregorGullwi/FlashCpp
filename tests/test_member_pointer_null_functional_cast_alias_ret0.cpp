// A functional cast that names a member-pointer alias must keep the alias
// target's class owner. A null cast then produces the same canonical
// member-pointer type as direct null initialization instead of reaching
// canonical identity construction without a published owner.
struct Owner {
	int value;
	int read() {
		return value;
	}
};

using MemberFunction = int (Owner::*)();
using MemberObject = int Owner::*;

int main() {
	MemberFunction null_function = MemberFunction(0);
	if (null_function != nullptr) {
		return 1;
	}
	if (!(null_function == nullptr)) {
		return 2;
	}
	MemberFunction null_copy = null_function;
	if (null_copy != nullptr) {
		return 3;
	}

	int (Owner::*direct_function)() = 0;
	if (null_function != direct_function) {
		return 4;
	}
	direct_function = MemberFunction(0);
	if (direct_function != nullptr) {
		return 5;
	}
	if (MemberFunction() != nullptr) {
		return 6;
	}

	MemberFunction read = MemberFunction(&Owner::read);
	if (read == nullptr) {
		return 7;
	}
	Owner object{42};
	if ((object.*read)() != 42) {
		return 8;
	}

	MemberObject null_member = MemberObject(0);
	if (null_member != nullptr) {
		return 9;
	}
	int Owner::*direct_member = 0;
	if (null_member != direct_member) {
		return 10;
	}
	MemberObject member = &Owner::value;
	if (member == nullptr) {
		return 11;
	}
	object.*member = 17;
	if ((object.*read)() != 17) {
		return 12;
	}
	return 0;
}
