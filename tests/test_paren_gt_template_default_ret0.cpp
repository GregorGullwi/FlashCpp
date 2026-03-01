// Test parenthesized greater-than operator in template parameter defaults
// Pattern from <functional>: bool _IsPlaceholder = (is_placeholder<_Arg>::value > 0)
template<typename T>
struct Traits {
    static constexpr int value = 1;
};

template<typename Arg,
         bool IsBindExpr = false,
         bool IsPlaceholder = (Traits<Arg>::value > 0)>
struct Mu;

template<typename Arg>
struct Mu<Arg, false, false> {
    int x;
};

template<typename Arg>
struct Mu<Arg, false, true> {
    int y;
};

int main() {
    Mu<int> m;
    (void)m;
    return 0;
}
