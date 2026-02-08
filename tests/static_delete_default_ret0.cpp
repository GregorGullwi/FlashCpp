// Test: = delete and = default on static member functions
// Validates: static void func() = delete; syntax

template<typename _Tp>
struct wrapper {
    static _Tp* _S_fun(_Tp& __r) noexcept { return &__r; }
    static void _S_fun(_Tp&&) = delete;
};

int main() {
    return 0;
}
