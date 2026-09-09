// Dependent members of type-only specializations remain distinct by primary and
// argument identity until substitution; Pair<T,U>::first is not Pair<U,T>::first.
struct Payload {
	short value;
};
template <typename T, typename U>
struct Pair {
	using first = T;
	using second = U;
	using nested = Payload;
};
template <typename T, typename U>
struct Hold {
	typename Pair<T, U>::first left;
	typename Pair<U, T>::first right;
	typename Pair<T, U>::nested nested;
};
template <typename RenamedT, typename RenamedU>
struct OtherHold {
	typename Pair<RenamedU, RenamedT>::first right;
	typename Pair<RenamedT, RenamedU>::first left;
};
int main() {
	Hold<int, double> a{};
	OtherHold<int, double> b{};
	a.left = 2;
	a.right = 3.5;
	a.nested.value = 7;
	b.left = 4;
	b.right = 5.5;
	return a.left + static_cast<int>(a.right) + a.nested.value +
		b.left + static_cast<int>(b.right) - 21;
}
