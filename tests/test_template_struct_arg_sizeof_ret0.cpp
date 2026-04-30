// Regression test for `computeTemplateTypeArgSizeBits` (src/TemplateRegistry_Types.h).
//
// When a class template is instantiated with a *complete* struct type, the
// substituted TypeSpecifierNode's size must reflect the real struct size — not
// the `0` sentinel that is now returned for incomplete/unresolved layouts.
//
// This pins the "complete struct → real size" branch so a future change that
// accidentally widens the deferral path (returning `0` for already-resolved
// structs) is caught here instead of silently miscompiling layout-dependent
// code.

struct Payload {
	int a;
	int b;
};

struct BigPayload {
	int values[8];
};

template <typename T>
struct Box {
	T stored;

	static constexpr int size_of_t() { return static_cast<int>(sizeof(T)); }
	static constexpr int size_of_box() { return static_cast<int>(sizeof(Box<T>)); }
};

// sizeof(T) inside the template body must resolve to the real struct size,
// which is what `makeTypeSpecifierFromTemplateTypeArg` ultimately depends on
// when it calls `computeTemplateTypeArgSizeBits` for struct-category args.
static_assert(Box<Payload>::size_of_t() == sizeof(Payload));
static_assert(Box<BigPayload>::size_of_t() == sizeof(BigPayload));

// The instantiated Box<T> itself must have a layout that includes the full
// struct member — i.e. the size carried through substitution was non-zero.
static_assert(sizeof(Box<Payload>) >= sizeof(Payload));
static_assert(sizeof(Box<BigPayload>) >= sizeof(BigPayload));

int main() {
	Box<Payload> small{};
	Box<BigPayload> big{};

	const bool small_ok =
		Box<Payload>::size_of_t() == static_cast<int>(sizeof(Payload)) &&
		Box<Payload>::size_of_box() >= static_cast<int>(sizeof(Payload));

	const bool big_ok =
		Box<BigPayload>::size_of_t() == static_cast<int>(sizeof(BigPayload)) &&
		Box<BigPayload>::size_of_box() >= static_cast<int>(sizeof(BigPayload));

	// Touch the members so the instantiated layout is actually used.
	small.stored.a = 1;
	small.stored.b = 2;
	big.stored.values[0] = 3;

	return (small_ok && big_ok && small.stored.a + small.stored.b + big.stored.values[0] == 6) ? 0 : 1;
}
