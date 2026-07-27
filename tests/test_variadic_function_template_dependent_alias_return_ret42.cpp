template <unsigned Index, typename Type>
struct Element;

template <typename... Types>
struct Tuple;

template <typename... Types>
struct TypeList {};

template <unsigned Index, typename Type>
using ElementT = typename Element<Index, Type>::type;

struct Payload {
	int value;
};

template <typename First, typename... Rest>
struct Tuple<First, Rest...> {
	First first;

	template <unsigned OtherIndex, typename... OtherTypes>
	friend ElementT<OtherIndex, Tuple<OtherTypes...>>& get(
		Tuple<OtherTypes...>& value);
};

template <typename First, typename... Rest>
struct Element<0, Tuple<First, Rest...>> {
	using type = First;
};

template <unsigned Index, typename... Types>
ElementT<Index, Tuple<Types...>>& get(Tuple<Types...>& value) {
	return value.first;
}

template <typename... Types>
ElementT<0, Tuple<Types...>>& first(Tuple<Types...>& value) {
	return value.first;
}

template <typename... Types>
ElementT<0, Tuple<Types...>>& chooseFirst(
	Tuple<Types...>& first_value,
	Tuple<Types...>&) {
	return first_value.first;
}

template <typename... Types>
TypeList<Types...>& identity(TypeList<Types...>& value) {
	return value;
}

int main() {
	Tuple<int, short, Payload> value{42};
	auto result = get<0>(value);
	auto first_result = first(value);
	auto repeated_result = chooseFirst(value, value);
	TypeList<> empty;
	auto& empty_result = identity(empty);
	(void) empty_result;
	return result == 42 && first_result == 42 && repeated_result == 42 ? 42 : 0;
}
