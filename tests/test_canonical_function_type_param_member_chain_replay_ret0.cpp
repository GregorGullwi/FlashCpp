// A published free function template must preserve canonical dependence while
// replaying a plain dependent member and chained type-only member template-ids.
struct ReplayMemberSource {
	using value_type = short;

	template <typename U>
	struct Rebind {
		template <typename V>
		struct Again {
			using type = V*;
		};
	};

	value_type value;
};

template <typename T>
int replay_member_chain(T source) {
	typename T::value_type copied = source.value;
	typename T::template Rebind<int>::template Again<long>::type pointer = nullptr;
	return static_cast<int>(copied) + (pointer == nullptr ? 0 : 1);
}

int main() {
	return replay_member_chain(ReplayMemberSource{6}) - 6;
}
