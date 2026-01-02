// Test reference qualifiers in template parameter defaults
// This tests both lvalue and rvalue references in template parameter defaults
// Pattern from <type_traits>: template<typename _Tp, typename _Up = _Tp&&>

template<typename T, typename U = T&&>
T forward_val(U val);

template<typename T, typename V = T&>
T copy_val(V val);

int main() {
    return 0;
}
