template <class... Types>
struct tuple {};

template <bool Same, class Dest, class... Sources>
constexpr bool tuple_conditional_explicit_v0 = false;

template <class... Dests, class... Sources>
constexpr bool tuple_conditional_explicit_v0<true, tuple<Dests...>, Sources...> = true;

int main() {
	static_assert(tuple_conditional_explicit_v0<true, tuple<int, long>, int, long>);
	static_assert(!tuple_conditional_explicit_v0<false, tuple<int, long>, int, long>);
	return tuple_conditional_explicit_v0<true, tuple<int>, int> ? 0 : 1;
}
