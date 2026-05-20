template <class Type, Type... Values>
struct integer_sequence {
	static constexpr Type size = sizeof...(Values);
};

template <unsigned long long Size>
using make_index_sequence = integer_sequence<unsigned long long, Size>;

template <class... Types>
using index_sequence_for = make_index_sequence<sizeof...(Types)>;

int main() {
	return index_sequence_for<char, int, long long>::size == 1 ? 0 : 1;
}
