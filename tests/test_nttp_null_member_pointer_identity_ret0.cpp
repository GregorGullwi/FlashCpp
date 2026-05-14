struct S {
	int x;
	int y;
	int f() {
		return x + y;
	}
};

constexpr int S::* g_null_member_obj = nullptr;

typedef int S::* member_obj_ptr;

template <member_obj_ptr Member>
struct member_obj_tag {
	static constexpr int value = 1;
};

template <>
struct member_obj_tag<nullptr> {
	static constexpr int value = 11;
};

template <>
struct member_obj_tag<&S::x> {
	static constexpr int value = 12;
};

template <member_obj_ptr Member>
int use_member_obj_tag() {
	return member_obj_tag<Member>::value;
}

int main() {
	if (member_obj_tag<nullptr>::value != 11) return 1;
	if (member_obj_tag<g_null_member_obj>::value != 11) return 2;
	if (member_obj_tag<&S::x>::value != 12) return 3;
	if (use_member_obj_tag<nullptr>() != 11) return 4;
	if (use_member_obj_tag<g_null_member_obj>() != 11) return 5;
	return 0;
}
