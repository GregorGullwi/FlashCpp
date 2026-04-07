// Regression: scoped-enum binary diagnostics must not reject a valid overloaded
// operator before overload resolution can use it.

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
} // namespace std_like

int main() {
	using namespace std_like;

	MemoryOrder base = MemoryOrder::acquire;
	MemoryOrder combined = base | hle_release;
	MemoryOrder extracted = combined & modifier_mask;

	if (int(combined) != (int(MemoryOrder::acquire) | int(hle_release)))
		return 1;
	if (int(extracted) != int(hle_release))
		return 2;

	return 0;
}
