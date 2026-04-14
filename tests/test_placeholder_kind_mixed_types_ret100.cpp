// Phase 4 test: mixed placeholder kinds with different native types
// Tests DependentArgs vs DependentMemberType classification across
// various type sizes and template patterns.
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> {
    using type = T;
};

// DependentArgs: simple template with dependent args (no ::)
template<typename T>
struct box {
    T val;
};

// DependentMemberType: dependent member alias (has ::)
template<typename T>
struct type_identity {
    using type = T;
};

template<typename T>
typename enable_if<true, T>::type make_val(T x) { return x; }

int main() {
    // Tests various types going through the placeholder system
    char c = make_val<char>(10);
    short s = make_val<short>(20);
    int i = make_val<int>(30);
    long long ll = make_val<long long>(40);

    box<int> b;
    b.val = c + s + i + ll;  // 10 + 20 + 30 + 40 = 100

    return b.val;
}
