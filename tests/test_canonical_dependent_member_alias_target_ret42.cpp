// A direct member alias target must retain the alias declaration's canonical
// parameter identity while its owning class remains dependent.
template <class Owner>
struct Provider {
	template <class Value>
	using Pointer = Value*;
};

template <class Owner, class Value>
using ProjectedPointer = typename Provider<Owner>::template Pointer<Value>;

int main() {
	ProjectedPointer<long, char> value = nullptr;
	return sizeof(*value) == sizeof(char) ? 42 : 0;
}
