// Regression: static_cast followed by pointer-to-member access (.* / ->*)
// MSVC type_traits _Invoker_pmf_object uses:
//   (static_cast<_Ty1&&>(_Arg1).*_Pmf)(...)
// .* / ->* are pm-expressions ([expr.mptr.oper]), applied after unary/cast
// by apply_pointer_to_member_operators — not postfix.

template <class Decayed, class Ty1, class... Types2>
struct Invoker {
	static constexpr auto call(Decayed pmf, Ty1&& arg1, Types2&&... args2)
		noexcept(noexcept((static_cast<Ty1&&>(arg1).*pmf)(static_cast<Types2&&>(args2)...)))
		-> decltype((static_cast<Ty1&&>(arg1).*pmf)(static_cast<Types2&&>(args2)...)) {
		return (static_cast<Ty1&&>(arg1).*pmf)(static_cast<Types2&&>(args2)...);
	}
};

int main() { return 0; }
