// Test multiline variadic macro with template angle brackets
// This tests that commas inside <> are preserved when a macro spans multiple lines

#define NOEXCEPT_IF(...) noexcept(__VA_ARGS__)

template<typename... Bs>
struct __and_ {
    static constexpr bool value = (Bs::value && ...);
};

template<typename T>
struct is_nothrow_move_constructible {
    static constexpr bool value = true;
};

template<typename T>
struct is_nothrow_move_assignable {
    static constexpr bool value = true;
};

template<typename _Tp>
void swap(_Tp& __a, _Tp& __b)
    NOEXCEPT_IF(__and_<is_nothrow_move_constructible<_Tp>,
                       is_nothrow_move_assignable<_Tp>>::value)
{
    _Tp __tmp = __a;
    __a = __b;
    __b = __tmp;
}

int main() {
    int a = 1, b = 2;
    swap(a, b);
    return (a == 2 && b == 1) ? 0 : 1;
}
