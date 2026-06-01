namespace std {
	using size_t = decltype(sizeof(0));
}

struct SpinPolicy {
	constexpr SpinPolicy() = default;
	int value = 0;
};

template<typename PoolTag>
struct WaiterBase {
	template<typename Pred, typename Spin = SpinPolicy>
	static bool do_spin(Pred pred, Spin spin = Spin{}) {
		std::size_t spin_size = sizeof(spin);
		return pred() && spin_size == sizeof(SpinPolicy);
	}
};

int main() {
	return WaiterBase<void>::do_spin([] { return true; }) ? 0 : 1;
}
