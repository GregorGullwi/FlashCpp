// Simplified test for noexcept(expr) in template parameter default

struct TestInvoke {
    static int _S_get() noexcept;
    
    template<typename Tp>
    static void _S_conv(Tp) noexcept;
    
    // noexcept with dependent template function call
    template<typename Tp,
             bool Nothrow = noexcept(_S_conv<Tp>(_S_get()))>
    static int _S_test(int);
};

int main() {
    return 0;
}
