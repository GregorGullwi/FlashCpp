// Spec-rooted member template-ids keep primary, owner-arg, and member-arg
// identity on the DependentInstantiation path; Holder<T,U>::Box<int> is not
// Holder<U,T>::Box<int> or Holder<T,U>::Box<short>.
struct Payload {
	short value;
};
template <typename T, typename U>
struct Holder {
	template <typename V>
	struct Box {
		using type = V*;
	};
	template <typename V>
	struct Cell {
		using type = V;
	};
};
template <typename T, typename U>
struct Hold {
	typename Holder<T, U>::template Box<int>::type ints;
	typename Holder<U, T>::template Box<int>::type swapped;
	typename Holder<T, U>::template Box<short>::type shorts;
	typename Holder<T, U>::template Cell<Payload>::type payload;
};
int main() {
	Hold<char, double> hold{};
	int value = 4;
	short narrow = 9;
	hold.ints = &value;
	hold.swapped = &value;
	hold.shorts = &narrow;
	hold.payload.value = 7;
	return *hold.ints + *hold.swapped + *hold.shorts + hold.payload.value - 24;
}
