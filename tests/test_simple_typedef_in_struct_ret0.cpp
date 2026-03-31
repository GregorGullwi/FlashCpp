// Test that a simple typedef inside a struct gets correct namespace-qualified
// name registration so type resolution works from outside the namespace.
//
// Gap: "typedef int MyInt;" inside a struct only registers the alias under
// its simple name ("MyInt"), making "Container::MyInt" and
// "ns::Container::MyInt" unresolvable.
//
// Return value is 0 on success.
namespace ns {
struct Container {
	typedef int MyInt;
};

int check(Container::MyInt v) {
	return v;
}
} // namespace ns

int main() {
	ns::Container::MyInt x = 0;
	return ns::check(x);
}
