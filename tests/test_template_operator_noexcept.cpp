// Test template function with noexcept and attributes
enum class byte : unsigned char {};

template<typename T>
using __byte_op_t = byte;

// Test operator<< with noexcept
template<typename _IntegerType>
    [[__gnu__::__always_inline__]]
    constexpr __byte_op_t<_IntegerType>
    operator<<(byte __b, _IntegerType __shift) noexcept
    { return (byte)(unsigned char)((unsigned)__b << __shift); }

// Test operator<<= with noexcept  
template<typename _IntegerType>
    constexpr byte&
    operator<<=(byte& __b, _IntegerType __shift) noexcept
    { return __b = __b << __shift; }

// Test regular function template with noexcept
template<typename T>
    [[nodiscard]]
    constexpr T add(T a, T b) noexcept
    { return a + b; }

int main() {
    byte b{42};
    int result = add(1, 2);
    return result;
}
