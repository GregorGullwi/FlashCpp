// Empty-pack expansion inside a variable-template initializer must yield ZERO
// arguments at the point of expansion, decided from the binding environment.
// Before the pack-aware replay fix, re-parsing the initializer saw the pack
// parameter as an unbound name and produced ONE dependent argument instead,
// so `PackCountV<>` instantiated the wrong specialization.

template <class... Ts>
struct CountBox {
	static constexpr unsigned value = sizeof...(Ts);
};

template <class... Ts>
constexpr unsigned CountBoxV = CountBox<Ts...>::value;

// Variable template whose initializer expands the pack into another
// template-id's argument list.
template <class... Ts>
constexpr unsigned MirrorV = CountBox<Ts...>::value;

// Function templates force the pack binding through the replay/substitution
// path: Us binds to zero arguments when called with no explicit arguments.
template <class... Us>
constexpr unsigned emptyCall() {
	return CountBoxV<Us...>;
}

template <class... Us>
constexpr unsigned singletonCall() {
	return MirrorV<Us..., char>;
}

struct TwoShorts {
	short a;
	short b;
};

int main() {
	// Direct empty-pack instantiation of a variable template.
	if (CountBoxV<> != 0u) {
		return 1;
	}
	// Empty-pack expansion nested in another variable-template initializer.
	if (MirrorV<> != 0u) {
		return 2;
	}
	// Singleton pack: exactly one argument reaches the class template.
	if (CountBoxV<TwoShorts> != 1u) {
		return 3;
	}
	if (MirrorV<long double> != 1u) {
		return 4;
	}
	// Non-empty pack through a function-template call (replay-bound pack).
	if (emptyCall<>() != 0u) {
		return 5;
	}
	if (emptyCall<TwoShorts, long double>() != 2u) {
		return 6;
	}
	// Mixed fixed prefix + trailing empty/singleton packs.
	if (singletonCall<int>() != 2u) {
		return 7;
	}
	return 0;
}
