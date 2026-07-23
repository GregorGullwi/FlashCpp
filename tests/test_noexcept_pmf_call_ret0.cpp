// Regression: static_cast followed by pointer-to-member access (.* / ->*)
// MSVC type_traits _Invoker_pmf_object uses:
//   (static_cast<_Ty1&&>(_Arg1).*_Pmf)(...)
// apply_postfix_operators must handle .* after cast expressions.

template <class Decayed, class Ty1, class... Types2>
struct Invoker {
	static constexpr auto call(Decayed pmf, Ty1&& arg1, Types2&&... args2)
		noexcept(noexcept((static_cast<Ty1&&>(arg1).*pmf)(static_cast<Types2&&>(args2)...)))
		-> decltype((static_cast<Ty1&&>(arg1).*pmf)(static_cast<Types2&&>(args2)...)) {
		return (static_cast<Ty1&&>(arg1).*pmf)(static_cast<Types2&&>(args2)...);
	}
};

int main() { return 0; }
