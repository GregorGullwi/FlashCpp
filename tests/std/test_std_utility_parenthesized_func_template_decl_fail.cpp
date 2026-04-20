// Expected failure: isolated MSVC <utility> parenthesized function-template
// declaration pattern. The real header uses this to protect min/max from
// function-like macros, including declarations shaped like:
//     template <class Ty> constexpr Ty(max)(initializer_list<Ty>);
//
// The std::initializer_list parameter and constexpr are not required to
// reproduce the parser error; the parenthesized function name under a template
// declaration is enough.

template <class T>
int (max)(T);

int main() {
	return 0;
}
