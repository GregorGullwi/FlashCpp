struct target_type {
	int value;
};

template <auto Member>
struct selected_member_value {
	static constexpr int value = 1;
};

template <>
struct selected_member_value<&target_type::value> {
	static constexpr int value = 42;
};

template <typename T>
struct member_alias_owner {
	static constexpr int T::* member = &T::value;
	template <auto Member>
	using alias = selected_member_value<Member>;
	using result = alias<member>;
};

int main() {
	return member_alias_owner<target_type>::result::value == 42 ? 0 : 1;
}
