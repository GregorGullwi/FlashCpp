// libstdc++ <type_traits> __make_unsigned_selector_base::__select: match a
// pack-peeled _List argument and default a bool NTTP from `_List::__size`.

struct SelectorBase {
	template <typename...>
	struct List {};

	template <typename Head, typename... Tail>
	struct List<Head, Tail...> : List<Tail...> {
		static constexpr unsigned long long size = sizeof(Head);
	};

	template <unsigned long long Sz, typename Tp, bool = (Sz <= Tp::size)>
	struct Select;

	template <unsigned long long Sz, typename Uint, typename... UInts>
	struct Select<Sz, List<Uint, UInts...>, true> {
		using type = Uint;
	};

	template <unsigned long long Sz, typename Uint, typename... UInts>
	struct Select<Sz, List<Uint, UInts...>, false>
		: Select<Sz, List<UInts...>> {};
};

int main() {
	using UInts = SelectorBase::List<unsigned char, unsigned short, unsigned int,
									 unsigned long, unsigned long long>;
	using ForChar = SelectorBase::Select<sizeof(char), UInts>::type;
	using ForInt = SelectorBase::Select<sizeof(int), UInts>::type;
	using ForLongLong = SelectorBase::Select<sizeof(long long), UInts>::type;
	return (sizeof(ForChar) == sizeof(unsigned char) &&
			sizeof(ForInt) == sizeof(unsigned int) &&
			sizeof(ForLongLong) == sizeof(unsigned long long))
			   ? 0
			   : 1;
}
