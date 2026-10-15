// A valid deferred decltype alias target whose qualified member call matches the
// instantiated arguments must still resolve; the argument validation added for
// the no-viable-overload case must not reject a viable call.

template <typename T>
struct Holder {
	static long pick(long value);
};

template <typename T>
using PickResult = decltype(Holder<T>::pick(0L));

int main() {
	return sizeof(PickResult<int>) == sizeof(long) ? 0 : 1;
}
