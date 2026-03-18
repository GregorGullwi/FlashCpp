// Test that using aliases for nested types get correct namespace-qualified name
// registration so ADL and type resolution work from outside the namespace.
// Uses scoped enum (enum class) which has full support in this compiler.
namespace ns {
	struct Container {
		enum class Status { Ok, Fail };
		using AliasStatus = Status;
	};
	// ADL: Container::Status is in namespace ns; this function should be
	// findable when called with a Container::AliasStatus argument.
	int check_alias_param(Container::AliasStatus s) {
		if (s == Container::Status::Ok) return 0;
		return 1;
	}
}
int main() {
	return ns::check_alias_param(ns::Container::Status::Ok);
}
