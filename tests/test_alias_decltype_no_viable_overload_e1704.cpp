// A deferred decltype alias target whose qualified member call exists but has no
// overload accepting the instantiated arguments must be rejected through the
// no-viable-function-call owner, not silently adopt the member's return type.

struct Payload {};

template <typename T>
struct Holder {
	static char pick(int* pointer);
};

template <typename T>
using PickResult = decltype(Holder<T>::pick(T{}));

int main() {
	return sizeof(PickResult<Payload>) == 1 ? 0 : 1;
}
