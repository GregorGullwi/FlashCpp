// Test: Nested ternary expressions in a deferred base class expression
// Ensures ExpressionSubstitutor handles recursive ternary substitution without
// dropping inner expression nodes when multiple ternaries are chained together.

template<typename T, T v>
struct integral_constant {
	static constexpr T value = v;
};

template<int Pn>
struct ternary_bucket
	: integral_constant<int,
		(Pn < -5) ? -2 :
		((Pn < 0) ? -1 :
		((Pn == 0) ? 0 : 1))> { };

template<int Pn>
struct ternary_adjusted
	: integral_constant<int,
		ternary_bucket<Pn>::value < 0
			? -ternary_bucket<Pn>::value
			: ternary_bucket<Pn>::value> { };

int main() {
	static_assert(ternary_bucket<-6>::value == -2);
	static_assert(ternary_bucket<-3>::value == -1);
	static_assert(ternary_bucket<0>::value == 0);
	static_assert(ternary_bucket<4>::value == 1);

	static_assert(ternary_adjusted<-6>::value == 2);
	static_assert(ternary_adjusted<-3>::value == 1);
	static_assert(ternary_adjusted<0>::value == 0);
	static_assert(ternary_adjusted<4>::value == 1);

	return 0;
}
