// Test member template alias with rvalue reference declarator
// This pattern is used in <type_traits> for __xref partial specializations

template<typename T>
struct Identity { using type = T; };

template<typename _Tp>
struct __xref {
    template<typename _Up> 
    using __type = typename Identity<_Tp>::type;
};

// Partial specialization with && pattern argument
template<typename _Tp>
struct __xref<_Tp&> {
    template<typename _Up> 
    using __type = typename Identity<_Tp>::type&;
};

// Partial specialization with && pattern argument and && in alias
template<typename _Tp>
struct __xref<_Tp&&> {
    template<typename _Up> 
    using __type = typename Identity<_Tp>::type&&;  // && as single token
};

int main() {
    return 0;
}
