// Test for variable template used as non-type argument in class template context
// This pattern was previously failing with "No primary template found" error
// because the parser tried to instantiate variable templates as class templates

namespace ns {
    // Variable template (like std::is_reference_v)
    template<typename _Tp>
    inline constexpr bool is_reference_v = false;
    
    template<typename _Tp>
    inline constexpr bool is_reference_v<_Tp&> = true;
    
    template<typename _Tp>
    inline constexpr bool is_reference_v<_Tp&&> = true;
    
    // Class template with bool non-type parameter (like std::enable_if)
    template<bool _Cond, typename _Tp = void>
    struct enable_if {};
    
    template<typename _Tp>
    struct enable_if<true, _Tp> {
        using type = _Tp;
    };
    
    // Template alias that produces a type
    template<typename _Xp>
    using condres_cvref = _Xp&;  // Returns a reference type
    
    // This is the pattern that was failing:
    // A class template that uses a variable template as a non-type argument
    template<typename _Xp, typename _Yp>
    struct common_ref_impl : enable_if<is_reference_v<condres_cvref<_Xp>>, condres_cvref<_Xp>> {};
}

int main() {
    return 0;
}
