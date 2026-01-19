#include <type_traits>
// Now include the bits headers that utility.h includes
#include <bits/move.h>

template<typename _Tp,
	   typename _Up = typename std::remove_cv<_Tp>::type,
	   typename = typename std::enable_if<std::is_same<_Tp, _Up>::value>::type,
	   size_t = std::tuple_size<_Tp>::value>
    using __enable_if_has_tuple_size = _Tp;

int main() {
    return 0;
}
