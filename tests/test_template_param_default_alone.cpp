// Test with std::remove_cv in template parameter default
#include <type_traits>

template<typename _Tp,
         typename _Up = typename std::remove_cv<_Tp>::type>
struct Test {};

int main() {
    return 0;
}
