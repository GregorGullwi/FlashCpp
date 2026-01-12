// Test member function templates inside partial specializations
// This was crashing with bad_any_cast before the fix

namespace std {

template<typename...>
using void_t = void;

// Primary template with 4 parameters
template<typename _Result, typename _Ret, bool _IsValid = false, typename = void>
struct __is_invocable_impl {};

// Partial specialization with a member function template
template<typename _Result, typename _Ret>
struct __is_invocable_impl<_Result, _Ret, false, void_t<typename _Result::type>>
{
    // Member function template inside partial specialization
    // This pattern is used in <type_traits> for is_invocable
    template<typename _Tp>
    static void _S_conv(_Tp) noexcept;
    
    template<typename _Tp>
    static int _S_test(int);
    
    template<typename _Tp>
    static int _S_test(...);
};

} // namespace std

int main() {
    return 0;
}
