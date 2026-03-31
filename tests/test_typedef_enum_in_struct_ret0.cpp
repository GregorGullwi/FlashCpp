// Test that typedef enum inside a struct gets correct namespace-qualified
// name registration so type resolution works from outside the namespace.
//
// Before fix: "typedef enum { ... } Alias;" inside a struct only registered
// the alias under its simple name ("Alias"), making "Container::Alias" and
// "ns::Container::Alias" unresolvable.
//
// Return value is 0 on success.
namespace ns {
struct Container {
	typedef enum { Ok,
				   Fail } Status;
};

 // Function taking the typedef'd enum by its struct-qualified name.
 // This exercises the "Container::Status" lookup in gTypesByName.
int check(Container::Status s) {
	if (s == Container::Ok)
		return 0;
	return 1;
}
} // namespace ns

int main() {
 // Fully qualified access from outside namespace:
 // requires "ns::Container::Status" to be registered in gTypesByName.
	ns::Container::Status s = ns::Container::Ok;
	return ns::check(s);
}
