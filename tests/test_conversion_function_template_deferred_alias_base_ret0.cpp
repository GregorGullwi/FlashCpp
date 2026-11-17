// A class-template base that is a non-deferred alias to a concrete record must
// be resolved during class instantiation, not left behind as a deferred base.
// Otherwise a dependent conversion cannot see the inherited operator.

struct ConversionRoot {
	template <class U>
	operator U() const {
		return U{};
	}
};

template <class T>
using ConversionBase = ConversionRoot;

template <class T>
struct AliasDerived : ConversionBase<T> {};

template <class T>
using ConversionBaseChain = ConversionBase<T>;

template <class T>
struct ChainedAliasDerived : ConversionBaseChain<T> {};

struct PlainRoot {
	operator int() const {
		return 0;
	}
};

template <class T>
using PlainBase = PlainRoot;

template <class T>
struct PlainAliasDerived : PlainBase<T> {};

template <class T>
int read_dependent(T value) {
	return value;
}

template <class T>
int cast_dependent(T value) {
	return static_cast<int>(value);
}

int main() {
	int direct = static_cast<int>(AliasDerived<int>{});
	int implicit = AliasDerived<int>{};
	int chained = read_dependent(ChainedAliasDerived<int>{});
	int dependent = read_dependent(AliasDerived<int>{});
	int explicit_dependent = cast_dependent(AliasDerived<int>{});
	int plain = PlainAliasDerived<int>{};
	return direct + implicit + chained + dependent + explicit_dependent + plain;
}
