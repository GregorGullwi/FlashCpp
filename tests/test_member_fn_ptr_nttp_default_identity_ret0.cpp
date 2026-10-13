struct Owner {
	int first() { return 1; }
	int second() { return 2; }
};

template <int (Owner::*Member)() = &Owner::first>
struct SelectMember {
	static constexpr int value = 0;
};

template <>
struct SelectMember<&Owner::first> {
	static constexpr int value = 11;
};

template <>
struct SelectMember<&Owner::second> {
	static constexpr int value = 22;
};

template <int (Owner::*Member)()>
struct MemberHolder {
	using type = SelectMember<Member>;
};

template <typename Left, typename Right>
struct IsSame {
	static constexpr bool value = false;
};

template <typename Type>
struct IsSame<Type, Type> {
	static constexpr bool value = true;
};

int main() {
	using Defaulted = SelectMember<>;
	using Explicit = SelectMember<&Owner::first>;
	using ThroughHolder = typename MemberHolder<&Owner::second>::type;
	if (Defaulted::value != 11) {
		return 1;
	}
	if (ThroughHolder::value != 22) {
		return 3;
	}
	return IsSame<Defaulted, Explicit>::value ? 0 : 2;
}
