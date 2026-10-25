// Regression: equally cv-qualified implicit objects prefer the rvalue-reference overload for an rvalue.
struct MemberRefRanking {
	int const_lvalue_vs_const_rvalue() const & { return 30; }
	int const_lvalue_vs_const_rvalue() const && { return 40; }
	static int mixed(double) { return 50; }
	int mixed(int) const { return 60; }
	static int only_static() { return 70; }
};

int main() {
	MemberRefRanking value;
	const MemberRefRanking constant;
	if (constant.only_static() != 70 || constant.mixed(1) != 60 || constant.mixed(1.0) != 50) return 2;
	return value.const_lvalue_vs_const_rvalue() == 30 && constant.const_lvalue_vs_const_rvalue() == 30 &&
		MemberRefRanking{}.const_lvalue_vs_const_rvalue() == 40 ? 0 : 1;
}
