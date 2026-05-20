struct replay_holder {
	template <typename U>
	static constexpr int replay() {
		return sizeof(U) + 1;
	}
};

template <typename T>
constexpr int replay_chain_v = replay_holder::template replay<T>();

int main() {
	static_assert(replay_chain_v<int> == 5, "variable-template initializer replay preserves dependent member-template chains");
	return replay_chain_v<int> - 5;
}
