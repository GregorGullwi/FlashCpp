#include <cstddef>

struct SpinPolicy {
	constexpr SpinPolicy() = default;
	int value = 0;
};

template<typename PoolTag>
struct WaiterBase {
	template<typename Pred, typename Spin = SpinPolicy>
	static bool do_spin(Pred pred, Spin spin = Spin{}) {
		return pred() && sizeof(spin) == sizeof(SpinPolicy);
	}
};

int main() {
	return WaiterBase<void>::do_spin([] { return true; }) ? 0 : 1;
}
