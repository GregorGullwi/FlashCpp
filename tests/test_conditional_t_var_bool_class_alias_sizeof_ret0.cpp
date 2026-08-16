// conditional_t / Trait::value as a class-member alias template argument must keep
// call-site arguments on a DependentArgs placeholder and rematerialize with the
// concrete class-template substitution (MSVC basic_string/_String_val shape).
template <bool B> struct BoolConstant { static constexpr bool value = B; };
using TrueType = BoolConstant<true>;
template <class T> struct IsSimpleAlloc : TrueType {};
template <class Alloc> constexpr bool IsSimpleAllocV = IsSimpleAlloc<Alloc>::value;
template <bool B, class T, class F> struct Conditional { using type = T; };
template <class T, class F> struct Conditional<false, T, F> { using type = F; };
template <bool B, class T, class F> using ConditionalT = typename Conditional<B, T, F>::type;
template <class T> struct SimpleTypes { using value_type = T; };
template <class Alloc>
struct ProbeTrait {
	using Selected = ConditionalT<IsSimpleAlloc<Alloc>::value, SimpleTypes<char>, SimpleTypes<long>>;
};
template <class Alloc>
struct ProbeV {
	using Selected = ConditionalT<IsSimpleAllocV<Alloc>, SimpleTypes<char>, SimpleTypes<long>>;
};
struct Alloc {};
int main() {
	using TraitSelected = ProbeTrait<Alloc>::Selected;
	using VarSelected = ProbeV<Alloc>::Selected;
	const unsigned trait_sz = sizeof(typename TraitSelected::value_type);
	const unsigned var_sz = sizeof(typename VarSelected::value_type);
	if (trait_sz == 1 && var_sz == 1) {
		return 0;
	}
	return (var_sz << 4) | trait_sz;
}
