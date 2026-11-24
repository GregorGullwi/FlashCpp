// [conv.qual]/1 includes pointer-to-member components in the qualification
// decomposition, so adding cv to the member type is a valid qualification
// conversion while [conv.qual]/3 rejects the reverse. A C-style cast may still
// perform the const_cast-like removal ([expr.cast]/4), and the resulting member
// pointer selects the same member. Reading through the const member pointer
// observes the stored value; only the C-style-stripped one may write.

struct Record {
	int value;
	int other;
};

using PlainMember = int Record::*;
using ConstMember = const int Record::*;

int main() {
	Record record{7, 9};
	PlainMember plain = &Record::value;
	ConstMember qualified = static_cast<ConstMember>(plain);
	ConstMember same = static_cast<ConstMember>(qualified);
	PlainMember c_style_stripped = (PlainMember)qualified;

	if (record.*qualified != 7 || record.*same != 7) {
		return 1;
	}
	record.*c_style_stripped = 11;
	if (record.*qualified != 11 || record.other != 9) {
		return 2;
	}
	record.*c_style_stripped = 17;
	if (record.*same != 17 || record.other != 9) {
		return 3;
	}
	return 0;
}
