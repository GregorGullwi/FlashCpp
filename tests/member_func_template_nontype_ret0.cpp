// Test: non-type template parameter in member function template body
// Box<4>::get() tests the class-template path; Converter::value<6>() tests
// the member-function-template path where the non-type param N must reach
// the body re-parse via template_param_substitutions_.
// Expected return: 4 + 6 - 10 = 0
template <int N>
struct Box {
	int get() { return N; }
};
struct Converter {
	template <int N>
	auto value() -> int { return N; }
};
int main() {
	Box<4> box;
	Converter c;
	return box.get() + c.value<6>() - 10;
}
