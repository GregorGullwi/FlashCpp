// Pack expansions feeding class-template bases must produce one instantiation
// identity per logical argument set across all phases (declaration parse,
// replay, recovery, codegen).  Before the binding-environment fix the replay
// pass re-classified an empty pack as one dependent argument, so
// `Base<Pack...>` instantiated with wrong arity and later passes re-derived a
// different instantiation for identical arguments.

template <bool Value>
struct BoolConstant {
	static constexpr bool value = Value;
};

template <class... Ts>
struct ArityOf : BoolConstant<false> {};

template <>
struct ArityOf<> : BoolConstant<true> {};

template <class First, class Second>
struct ArityOf<First, Second> : BoolConstant<true> {};

// Variable template reading ::value through a deferred base of ArityOf.
template <class... Ts>
constexpr bool ArityKnownV = ArityOf<Ts...>::value;

// Class template whose base list expands the pack.
template <class... Ts>
struct Holder : ArityOf<Ts...> {};

// Function template wrappers force substitution through replayed bodies.
template <class... Us>
constexpr bool viaFunction() {
	return ArityKnownV<Us...>;
}

template <class... Us>
constexpr bool viaHolder() {
	return Holder<Us...>::value;
}

struct Small {
	char data;
};

int main() {
	// Empty pack: zero arguments -> primary specialization -> true.
	if (!ArityKnownV<>) {
		return 1;
	}
	if (!viaFunction<>()) {
		return 2;
	}
	if (!viaHolder<>()) {
		return 3;
	}
	// Singleton pack: matches no listed arity pattern -> false.
	if (ArityKnownV<Small>) {
		return 4;
	}
	if (viaHolder<Small>()) {
		return 5;
	}
	// Two-element pack: exact two-arg specialization -> true.
	if (!viaFunction<Small, int>()) {
		return 6;
	}
	if (!ArityKnownV<long double, short>) {
		return 7;
	}
	// Three elements: falls back to primary -> false.
	if (viaHolder<Small, int, char>()) {
		return 8;
	}
	// Same logical arguments used twice must stay consistent within one
	// compilation (single instantiation identity).
	const bool first = viaFunction<Small>();
	const bool second = ArityKnownV<Small>;
	if (first != second) {
		return 9;
	}
	return 0;
}
