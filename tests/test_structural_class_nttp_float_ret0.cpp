struct FloatKey {
	float value;
};

template <FloatKey K>
struct tag {
	static constexpr int value = 0;
};

constexpr FloatKey key{1.5f};
constexpr FloatKey equivalent_key{1.5f};

template <>
struct tag<key> {
	static constexpr int value = 1;
};

int main() {
	return tag<equivalent_key>::value == 1 ? 0 : 1;
}
