template <typename T>
struct replay_traits {
	template <typename U>
	struct box {
		static constexpr int get() {
			return sizeof(T) == sizeof(U) ? 0 : 1;
		}
	};
};

template <typename T>
constexpr int replay_member_template_call_v = replay_traits<T>::template box<T>::get();

int main() {
	static_assert(replay_member_template_call_v<int> == 0, "variable-template replay preserves dependent member-template call chains");
	return replay_member_template_call_v<int>;
}
