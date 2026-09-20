// A qualified member-alias use whose owner is dependent carries the published
// member alias declaration identity, so the canonical resolver auto-redirects
// it once the owner qualifier is concrete. The target captures the enclosing
// owner argument, so `P<int, char>` is `int*`, not `char*`; the return value
// distinguishes owner-then-alias argument resolution.
template <class Owner>
struct Captures {
	template <class Value>
	using Pointer = Owner*;
};

template <class Owner, class Value>
using P = typename Captures<Owner>::template Pointer<Value>;

int main() {
	P<int, char> pointer = nullptr;
	return sizeof(*pointer) == sizeof(int) ? 42 : 0;
}
