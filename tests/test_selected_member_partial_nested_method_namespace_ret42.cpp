// Namespace-qualified selected member partials must mangle published nested
// methods with the same declaring-owner namespace as their call sites.
namespace ns {
template<class T>
struct Owner {
	template<class U>
	struct Box {};

	template<class U>
	struct Box<U*> {
		struct Child {
			T echo(T value) { return value; }
		};
	};
};
}

using Box = ns::Owner<long long>::Box<int*>;
using Child = Box::Child;

int main() {
	Child child{};
	if (child.echo(5000000000LL) != 5000000000LL) return 1;
	return 42;
}
