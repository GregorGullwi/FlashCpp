struct S {
	int x;
	int y;
	int f() {
		return x + y;
	}
};

constexpr int S::* g_null_member_obj = nullptr;
constexpr int (S::* g_null_member_fn)() = nullptr;

template <int S::* Member>
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

template <int (S::* MemberFn)()>
struct member_fn_tag {
	static constexpr int value = 21;
};

template <>
struct member_fn_tag<nullptr> {
	static constexpr int value = 31;
};

template <>
struct member_fn_tag<&S::f> {
	static constexpr int value = 32;
};

template <int S::* Member>
int use_member_obj_tag() {
	return member_obj_tag<Member>::value;
}

template <int (S::* MemberFn)()>
int use_member_fn_tag() {
	return member_fn_tag<MemberFn>::value;
}

int main() {
	if (member_obj_tag<nullptr>::value != 11) return 1;
	if (member_obj_tag<g_null_member_obj>::value != 11) return 2;
	if (member_obj_tag<static_cast<int S::*>(nullptr)>::value != 11) return 3;
	if (member_obj_tag<&S::x>::value != 12) return 4;
	if (use_member_obj_tag<nullptr>() != 11) return 5;
	if (use_member_obj_tag<g_null_member_obj>() != 11) return 6;

	if (member_fn_tag<nullptr>::value != 31) return 7;
	if (member_fn_tag<g_null_member_fn>::value != 31) return 8;
	if (member_fn_tag<static_cast<int (S::*)()>(nullptr)>::value != 31) return 9;
	if (member_fn_tag<&S::f>::value != 32) return 10;
	if (use_member_fn_tag<nullptr>() != 31) return 11;
	if (use_member_fn_tag<g_null_member_fn>() != 31) return 12;

	return 0;
}
