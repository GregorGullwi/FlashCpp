struct FloatKey {
	float value;
};

template <FloatKey K>
constexpr int read_value() {
	return K.value == 1.5f ? 0 : 1;
}

constexpr FloatKey key{1.5f};

int main() {
	return read_value<key>();
}
