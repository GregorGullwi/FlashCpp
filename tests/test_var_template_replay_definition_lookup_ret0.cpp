constexpr int replay_probe(long) {
	return 0;
}

template <typename T>
constexpr int replay_lookup_v = replay_probe(sizeof(T));

constexpr int replay_probe(int) {
	return 1;
}

int main() {
	static_assert(replay_lookup_v<char> == 0, "ordinary lookup for the variable-template initializer is definition-bound");
	return replay_lookup_v<char>;
}
