// In-class Spec-rooted stamping covers CurrentInstantiation (self primary on the
// out-of-line path) and UnknownSpecialization (other primary / swapped args).
// Box<T,U>::first is not Pair<U,T>::first.
struct Payload {
	short value;
};
template <typename T, typename U>
struct Pair {
	using first = T;
	using second = U;
};
template <typename T, typename U>
struct Box {
	using first = T;
	using second = U;
	using nested = Payload;
	first self_left;
	second self_right;
	nested self_nested;
	typename Pair<T, U>::first other_left;
	typename Pair<U, T>::first other_swapped;
	typename Box<T, U>::first get_self();
};
template <typename T, typename U>
typename Box<T, U>::first Box<T, U>::get_self() {
	return self_left;
}
int main() {
	Box<int, double> a{};
	a.self_left = 2;
	a.self_right = 3.5;
	a.self_nested.value = 7;
	a.other_left = 4;
	a.other_swapped = 5.5;
	return a.get_self() + static_cast<int>(a.self_right) + a.self_nested.value +
		a.other_left + static_cast<int>(a.other_swapped) - 21;
}
