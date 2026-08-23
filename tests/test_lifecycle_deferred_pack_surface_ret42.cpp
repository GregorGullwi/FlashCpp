// Phase 4 lifecycle regression: a member-function-template body is allowed
// to retain parser-owned fold and pack helpers while it remains deferred.
// The class is materialized, but pending<Ts...> is never selected, so its
// unresolved pack must not be sent through semantic normalization early.

template <typename... OwnerTypes>
struct DeferredSurface {
	template <typename... CallTypes>
	static int pending(CallTypes... values) {
		return (0 + ... + values) + static_cast<int>(sizeof...(OwnerTypes));
	}
};

int main() {
	DeferredSurface<short, long long> value;
	return sizeof(value) == 1 ? 42 : 0;
}
