// Test file for template alias declarations with requires clauses
// Both global and member template aliases with requires constraints should parse

namespace ns {
    template<typename _Tp>
    inline constexpr bool is_reference_v = false;
    
    template<typename _Tp>
    inline constexpr bool is_reference_v<_Tp&> = true;
    
    template<typename _Tp>
    inline constexpr bool is_reference_v<_Tp&&> = true;
    
    // Global template alias with requires clause
    // Pattern from <type_traits>: template<typename _Xp, typename _Yp>
    //     requires is_reference_v<__condres_cvref<_Xp, _Yp>>
    // using __common_ref_impl = ...;
    template<typename _Xp, typename _Yp>
        requires is_reference_v<_Xp>  
    using CondresRef = _Xp;
    
    // Template struct with member template alias with requires clause
    template<typename T>
    struct Test {
        template<typename U>
            requires is_reference_v<U>
        using ValueType = U;
    };
}

int main() {
    // Parsing verification - if this compiles, the requires clause + template alias parsing works
    return 0;
}
