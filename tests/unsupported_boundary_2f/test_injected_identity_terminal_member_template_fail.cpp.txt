// Holder<T>::template Box<T> must resolve to Box, not stop at Holder.
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
