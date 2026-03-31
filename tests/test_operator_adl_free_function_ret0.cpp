// Operator ADL: regular free-function operators in associated namespaces.
// Per C++20 [over.match.oper]/2, ADL should find non-member operator
// overloads declared in associated namespaces even when they are not
// brought into the calling scope via 'using'.
//
// Return value is 0 on success.

namespace ns {
struct S {
	int x;
};

 // Regular free function — NOT a hidden friend.
 // ADL should find this when called with ns::S arguments.
bool operator==(S a, S b) { return a.x == b.x; }
bool operator!=(S a, S b) { return a.x != b.x; }
} // namespace ns

int main() {
	ns::S a{};
	ns::S b{};

 // ADL should find ns::operator== because both arguments are ns::S.
	if (!(a == b))
		return 1;

	a.x = 42;
	if (a == b)
		return 2;
	if (!(a != b))
		return 3;

	b.x = 42;
	if (a != b)
		return 4;

	return 0;
}
