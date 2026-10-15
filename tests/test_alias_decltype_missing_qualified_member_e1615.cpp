// A deferred decltype alias target whose qualified call names a member that does
// not exist on the instantiated owner is ill-formed. It must be rejected through
// the undeclared-qualified-identifier owner instead of silently resolving to a
// placeholder type.

template <typename T>
struct Holder {};

template <typename T>
using MissingMember = decltype(Holder<T>::missing());

int main() {
	return sizeof(MissingMember<int>) == 1 ? 0 : 1;
}
