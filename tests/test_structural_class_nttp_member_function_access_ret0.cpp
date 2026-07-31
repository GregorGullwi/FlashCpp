struct FloatKey {
	float value;
};

constexpr FloatKey key{1.5f};

template <typename T>
struct Reader {
	template <FloatKey K>
	constexpr int read() const {
		return K.value == 1.5f ? 0 : 1;
	}
};

int main() {
	return Reader<int>{}.read<key>();
}
