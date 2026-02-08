// Test that pack_param_info_ is properly saved/restored across
// nested variadic template instantiations. When an inner template
// instantiation takes the fallback (no body position) path, the
// outer template's pack info must not be lost.

template<typename T>
T identity(T x) {
	return x;
}

template<typename First>
int var_sum(First first) {
	return identity(first);
}

template<typename First, typename... Rest>
int var_sum(First first, Rest... rest) {
	// The inner call to var_sum(rest...) triggers recursive template
	// instantiation. If pack_param_info_ isn't restored on return,
	// subsequent sizeof... or pack expansion in the outer context breaks.
	return first + var_sum(rest...);
}

int main() {
	// var_sum(1,2,3,4,5) = 1+2+3+4+5 = 15
	return var_sum(1, 2, 3, 4, 5);
}
