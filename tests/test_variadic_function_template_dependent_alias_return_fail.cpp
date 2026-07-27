template <unsigned Index, typename Type>
struct Element;

template <typename... Types>
struct Tuple;

template <typename First, typename... Rest>
struct Tuple<First, Rest...> {
	First first;
};

template <typename First, typename... Rest>
struct Element<0, Tuple<First, Rest...>> {
	using type = First;
};

template <unsigned Index, typename Type>
using ElementT = typename Element<Index, Type>::type;

template <unsigned Index, typename... Types>
ElementT<Index, Tuple<Types...>>& get(Tuple<Types...>& value) {
	return value.first;
}

int main() {
	Tuple<int, float, double> value{42};
	auto result = get<0>(value);
	return result;
}
