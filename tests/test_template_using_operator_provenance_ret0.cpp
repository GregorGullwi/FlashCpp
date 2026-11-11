// A using-declaration that names an operator in a dependent base must retain
// the source base identity through class-template instantiation. Alias-spelled
// bases use the same declaration-order identity rather than the instantiated
// type spelling.

template <typename T>
struct Base {
	int value;
	int operator-() const { return -value; }
};

template <typename T>
struct AliasBaseTarget {
	int value;
	int operator-() const { return -value; }
};

template <typename T>
using AliasBase = AliasBaseTarget<T>;

struct Padding {
	long long guard;
};

template <typename T>
struct Hidden : Padding, Base<T> {
	int operator-() && { return -2; }
};

template <typename T>
struct Restored : Padding, Base<T> {
	using Base<T>::operator-;
	int operator-() && { return -3; }
};

template <typename T>
struct AliasRestored : Padding, AliasBase<T> {
	using AliasBase<T>::operator-;
	int operator-() && { return -4; }
};

template <typename T>
concept HasUnaryMinus = requires(T value) { -value; };

static_assert(!HasUnaryMinus<Hidden<short>>);
static_assert(HasUnaryMinus<Restored<short>>);
static_assert(HasUnaryMinus<AliasRestored<short>>);

int main() {
	Restored<short> direct{};
	direct.guard = 9999;
	direct.value = 7;
	if (-direct != -7) return 1;

	AliasRestored<short> aliased{};
	aliased.guard = 9999;
	aliased.value = 11;
	if (-aliased != -11) return 2;

	return 0;
}
