// A member-pointer owner that resolves to a non-class type is ill-formed and
// must report MemberPointerOwnerNotClass rather than failing internally during
// canonical member-pointer construction.

using Int = int;

int Int::* pointer;

int main() {
	(void)pointer;
	return 0;
}
