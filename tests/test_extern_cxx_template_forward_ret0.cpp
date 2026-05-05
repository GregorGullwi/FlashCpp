template <class _Elem>
struct char_traits {};

extern "C++" template <class _Elem, class _Traits = char_traits<_Elem>>
class basic_ios;

template <class _Elem, class _Traits>
class basic_ios {
public:
	_Elem value;
};

int main() {
	basic_ios<int> io{0};
	return io.value;
}
