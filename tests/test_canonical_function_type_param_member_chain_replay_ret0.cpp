// A published free function template must preserve canonical dependence while
// replaying both a plain dependent member type and a type-only member template-id.
struct ReplayMemberSource {
	using value_type = short;

	template <typename U>
	struct Rebind {
		using type = U*;
	};

	value_type value;
};

template <typename T>
int replay_member_chain(T source) {
	typename T::value_type copied = source.value;
	typename T::template Rebind<int>::type pointer = nullptr;
	return static_cast<int>(copied) + (pointer == nullptr ? 0 : 1);
}

int main() {
	return replay_member_chain(ReplayMemberSource{6}) - 6;
}
