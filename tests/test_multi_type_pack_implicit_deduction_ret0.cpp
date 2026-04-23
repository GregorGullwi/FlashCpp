// Two variadic type packs in one template; only Ts maps to the function-parameter pack.
// Calling multi_type_pack_func(1, 2, 3) implicitly:
//   Tags = {} (empty – not deducible from call args, treated as empty pack)
//   Ts   = {int, int, int} (deduced from the three call arguments)
//   Returns sizeof...(Tags) * 10 + sizeof...(Ts) = 0 * 10 + 3 = 3

template<typename... Tags, typename... Ts>
int multi_type_pack_func(Ts...) {
	return static_cast<int>(sizeof...(Tags)) * 10 + static_cast<int>(sizeof...(Ts));
}

int main() {
	int a = multi_type_pack_func(1, 2, 3);
	if (a != 3) return 1;
	return 0;
}
