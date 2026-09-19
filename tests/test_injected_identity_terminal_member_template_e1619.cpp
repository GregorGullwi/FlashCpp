// Holder<T>::template Box<T> must resolve to Box, not stop at Holder.
//
// Box<int> has an extra int member and is larger than Holder<int>, so the
// static_assert condition is false and reports StaticAssertFailure#1619.
// If the terminal member template incorrectly resolved as its Holder owner,
// the two sizes would match and the assertion would wrongly pass.
template <class T>
struct Holder {
	T owner;

	template <class U>
	struct Box {
		U member;
		int box_kind;
	};

	template <class U>
	struct Late {
		typename Holder<T>::template Box<T>
		terminal(typename Holder<T>::template Box<T> value) {
			return value;
		}
	};
};

Holder<int>::Late<char> late_value;
Holder<int>::Box<int> box_value;
static_assert(
	sizeof(decltype(late_value.terminal(box_value))) == sizeof(Holder<int>),
	"the terminal member template must not resolve as its Holder owner");

int main() {
	return 0;
}
