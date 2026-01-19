#include <type_traits>

template<typename _Tp,
	   typename _Up = typename std::remove_cv<_Tp>::type,
	   typename = typename std::enable_if<std::is_same<_Tp, _Up>::value>::type>
    using test_alias = _Tp;

int main() {
    return 0;
}
