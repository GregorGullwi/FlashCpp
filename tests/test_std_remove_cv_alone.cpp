// Test with std::remove_cv from type_traits
#include <type_traits>

template<typename _Tp>
using test1 = typename std::remove_cv<_Tp>::type;

int main() {
    return 0;
}
