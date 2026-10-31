// The same pack is nested in a List argument and expanded by the outer pattern.
struct Payload { long long first; long long second; };
template<class A, class B> struct Pair {
	A first;
	B second;
};
template<class T> struct Wrap {
	T value;
};
template<class... T> struct List {
	long long storage[sizeof...(T) + 1];
	int marker;
	int count() const { return sizeof...(T); }
	int bytes() const { return (0 + ... + sizeof(T)); }
};
template<class... Outer> struct Owner {
	template<class Tag> struct Box {
		List<Pair<Outer, List<Outer...>>...> values;
		List<Wrap<Wrap<Outer>>...> wrapped;
	};
};
int main() {
	Owner<char, Payload, double>::Box<int> full{};
	Owner<>::Box<long long> empty{};
	if (full.values.count() != 3 || full.wrapped.count() != 3) return 1;
	const int full_bytes = sizeof(Pair<char, List<char, Payload, double>>) +
		sizeof(Pair<Payload, List<char, Payload, double>>) + sizeof(Pair<double, List<char, Payload, double>>);
	if (full.values.bytes() != full_bytes) return 2;
	const int wrapped_bytes = sizeof(Wrap<Wrap<char>>) + sizeof(Wrap<Wrap<Payload>>) + sizeof(Wrap<Wrap<double>>);
	if (full.wrapped.bytes() != wrapped_bytes) return 3;
	if (empty.values.count() != 0 || empty.values.bytes() != 0) return 4;
	if (empty.wrapped.count() != 0 || empty.wrapped.bytes() != 0) return 5;
	full.values.marker = 7;
	full.wrapped.marker = 11;
	empty.values.marker = 13;
	empty.wrapped.marker = 17;
	return full.values.marker + full.wrapped.marker + empty.values.marker + empty.wrapped.marker - 48;
}
