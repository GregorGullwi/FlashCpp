struct S {
	int f() {
		return 1;
	}
	int g() {
		return 2;
	}
};

template <int (S::*MemberFn)()>
struct member_fn_selector {
	static constexpr int value = 0;
};

template <>
struct member_fn_selector<&S::f> {
	static constexpr int value = 11;
};

template <>
struct member_fn_selector<&S::g> {
	static constexpr int value = 22;
};

int main() {
	if (member_fn_selector<&S::f>::value != 11) return 1;
	if (member_fn_selector<&S::g>::value != 22) return 2;
	return 0;
}
