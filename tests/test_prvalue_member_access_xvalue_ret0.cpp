struct S { int x; };
S make_s() { return S{42}; }

template<class T> struct is_rvalue_ref { static constexpr bool value = false; };
template<class T> struct is_rvalue_ref<T&&> { static constexpr bool value = true; };

int main() {
	// make_s() is a prvalue, .x is non-arrow member access
	// C++20 [expr.ref]/4: prvalue object → xvalue → decltype((E)) gives T&&
	return is_rvalue_ref<decltype((make_s().x))>::value ? 0 : 1;
}
