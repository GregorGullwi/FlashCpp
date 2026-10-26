template<unsigned I> struct Cell { short value; };
struct Padding { long long guard; };
template<unsigned I> struct Next : Padding, Cell<I + 1> {};
template<auto Value> struct Deduced {};
template<int Value> struct Fixed {};
template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
template<unsigned Value> int select(Cell<Value>*) { return 1; }
int select(Cell<1u>*) { return 2; }
int main() {
	Next<0u> object;
	object.guard = 12345678901LL;
	object.value = 41;
	Cell<1u>* base = &object;
	if (base->value != 41 || object.guard != 12345678901LL) return 1;
	if (!Same<Fixed<1>, Fixed<1u>>::value) return 2;
	if (Same<Deduced<1>, Deduced<1u>>::value) return 3;
	if (select(base) != 2) return 4;
	return 0;
}
