template <unsigned long long N>
struct pack_count {
	static constexpr unsigned long long value = N;
};

template <class... Types>
using count_types = pack_count<sizeof...(Types)>;

template <class... Types>
using forward_count_types = count_types<Types...>;

int main() {
	const bool non_empty_ok = forward_count_types<char, int, long long>::value == 3;
	const bool empty_ok = forward_count_types<>::value == 0;
	return (non_empty_ok && empty_ok) ? 0 : 1;
}
