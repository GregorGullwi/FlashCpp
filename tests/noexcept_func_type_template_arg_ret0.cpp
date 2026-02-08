// Test: noexcept(expr) on function types in template arguments
// Validates: _Res(_ArgTypes...) noexcept(_NE) syntax

template<typename _Functor>
struct _Weak_result_type_impl {};

template<typename _Res, typename... _ArgTypes, bool _NE>
struct _Weak_result_type_impl<_Res(_ArgTypes...) noexcept (_NE)> {
    typedef _Res result_type;
};

template<typename _Res, typename... _ArgTypes, bool _NE>
struct _Weak_result_type_impl<_Res(*)(_ArgTypes...) noexcept (_NE)> {
    typedef _Res result_type;
};

int main() {
    return 0;
}
