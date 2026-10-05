// A concept requirement reached through the lazy constraint evaluator receives
// its operand from flat template-argument storage, which carries a
// member-object-pointer's declaring-class owner only as a spelling. The owner
// identity must be published before canonical import, or
// `__is_member_object_pointer` is answered by the compatibility switch instead
// of the canonical type. This regression isolates that operand shape across a
// native scalar member, a differently sized native member, and a record member,
// so the canonical-structural-trait fallback stays at zero.
struct Host {
	int field;
	long tag;
	void bump();
};

struct Other {
	short value;
};

void Host::bump() {}

template <typename Type>
concept MemberObjectPointerish = __is_member_object_pointer(Type);

int probeMemberPointer(MemberObjectPointerish auto) { return 30; }
int probeMemberPointer(...) { return 31; }

int main() {
	int Host::* field_pointer = &Host::field;
	long Host::* tag_pointer = &Host::tag;
	short Other::* value_pointer = &Other::value;
	int scalar = 0;

	if (probeMemberPointer(field_pointer) != 30) {
		return 1;
	}
	if (probeMemberPointer(tag_pointer) != 30) {
		return 2;
	}
	if (probeMemberPointer(value_pointer) != 30) {
		return 3;
	}
	if (probeMemberPointer(scalar) != 31) {
		return 4;
	}
	return 0;
}
