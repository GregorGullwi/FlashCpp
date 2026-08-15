// Regression: auto deduction from a traits static `find` that returns a pointer.
// Definition-time deduction must defer when the owner is a template parameter.

template <class Char>
struct CharTraits {
	using char_type = Char;

	static const Char* find(const Char* first, unsigned long long, const Char&) {
		return first;
	}
};

template <class Traits>
int traits_find_head(
	const typename Traits::char_type* haystack,
	unsigned long long hay_size,
	typename Traits::char_type ch) {
	(void)hay_size;
	(void)ch;
	const auto found_at = Traits::find(haystack, hay_size, ch);
	return found_at == haystack ? 0 : 1;
}

int main() {
	const char letters[] = "hello";
	const wchar_t wide[] = L"world";
	return traits_find_head<CharTraits<char>>(letters, 5, 'h') +
		   traits_find_head<CharTraits<wchar_t>>(wide, 5, L'w');
}
