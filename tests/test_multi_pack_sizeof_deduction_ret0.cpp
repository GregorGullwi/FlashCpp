// Scenario C: two variadic packs of different kinds in the same template.
// Ns is a non-type int pack that is fully explicit (no function parameter).
// Ts is a type pack that is expanded in the function parameter list and deduced
// from the call arguments.
//
// mixed_kind_packs<1, 2, 3>(100, 200):
//   Ns = {1, 2, 3}  (3 explicit non-type args)
//   Ts = {int, int} (deduced from the two int call args)
//   returns sizeof...(Ns) * 10 + sizeof...(Ts)  =  3*10 + 2  =  32
//
// This tests that sizeof...(Ns) resolves to 3 (not to the total 5) and
// sizeof...(Ts) resolves to 2 (not to the total 5) when multiple packs share
// the same template_args flat list.

template<int... Ns, typename... Ts>
int mixed_kind_packs(Ts...) { return static_cast<int>(sizeof...(Ns)) * 10 + static_cast<int>(sizeof...(Ts)); }

int main() {
	int c = mixed_kind_packs<1, 2, 3>(100, 200);
	if (c != 32) return 1;
	return 0;
}
