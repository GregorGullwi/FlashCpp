// Regression: mixed scoped/unscoped enum operator overloads must stay viable
// through sema and functional-cast parsing, preserving the enum type metadata
// that libstdc++'s atomic_wait helpers rely on.

namespace std_like {
enum class MemoryOrder : int {
	relaxed = 0,
	acquire = 2,
	release = 3
};

enum MemoryOrderModifier {
	mask = 0x0ffff,
	modifier_mask = 0xffff0000,
	hle_acquire = 0x10000,
	hle_release = 0x20000
};

constexpr MemoryOrder operator|(MemoryOrder order, MemoryOrderModifier modifier) {
	return MemoryOrder(int(order) | int(modifier));
}

constexpr MemoryOrder operator&(MemoryOrder order, MemoryOrderModifier modifier) {
	return MemoryOrder(int(order) & int(modifier));
}

constexpr MemoryOrder foldOrder(MemoryOrder order) {
	return order == MemoryOrder::release ? MemoryOrder::acquire : order;
}
} // namespace std_like

int main() {
	using namespace std_like;

	MemoryOrder base = MemoryOrder::acquire;
	MemoryOrder combined = base | hle_release;
	MemoryOrder extracted = combined & modifier_mask;
	MemoryOrder nested = MemoryOrder(foldOrder(combined & mask) | MemoryOrderModifier(combined & modifier_mask));

	if (int(combined) != (int(MemoryOrder::acquire) | int(hle_release)))
		return 1;
	if (int(extracted) != int(hle_release))
		return 2;
	if (int(nested) != (int(MemoryOrder::acquire) | int(hle_release)))
		return 3;

	return 0;
}
