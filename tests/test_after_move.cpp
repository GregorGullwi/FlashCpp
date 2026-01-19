// Test with std::remove_cv in template parameter default AFTER bits/move.h
#include <type_traits>
#include <bits/move.h>

template<typename _Tp,
         typename _Up = typename std::remove_cv<_Tp>::type>
struct Test {};

int main() {
    return 0;
}
