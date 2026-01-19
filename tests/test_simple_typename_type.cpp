// Test to understand the parse failure
// This should work
template<typename T> struct remove_cv { using type = T; };

template<typename _Tp>
using test1 = typename remove_cv<_Tp>::type;

int main() {
    return 0;
}
