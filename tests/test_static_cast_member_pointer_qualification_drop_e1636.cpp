// [conv.qual]/1 lists "pointer to member of class C of type" among the P
// components of a qualification decomposition, and [conv.qual]/4 repeats the
// member-type cv rule. Casting "const int Record::*" to "int Record::*"
// therefore drops cv on the member type at the same point [conv.qual]/3
// rejects it for an object pointer, so [expr.static.cast]/5 must reject it.

struct Record {
	int value;
};

using PlainMember = int Record::*;
using ConstMember = const int Record::*;

int main() {
	ConstMember source = &Record::value;
	PlainMember target = static_cast<PlainMember>(source);
	(void)target;
	return 0;
}
