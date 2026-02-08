// Test: Member function pointer types in template specialization patterns
// Validates: _Res (_Class::*)(_ArgTypes...) syntax in template args

template<typename _Signature>
struct _Mem_fn_traits;

template<typename _Res, typename _Class, typename... _ArgTypes>
struct _Mem_fn_traits<_Res (_Class::*)(_ArgTypes...)> {
    using result_type = _Res;
};

template<typename _Res, typename _Class, typename... _ArgTypes>
struct _Mem_fn_traits<_Res (_Class::*)(_ArgTypes...) const> {
    using result_type = _Res;
};

template<typename _Res, typename _Class, typename... _ArgTypes>
struct _Mem_fn_traits<_Res (_Class::*)(_ArgTypes...) const &> {
    using result_type = _Res;
};

template<typename _Res, typename _Class, typename... _ArgTypes>
struct _Mem_fn_traits<_Res (_Class::*)(_ArgTypes...) && noexcept> {
    using result_type = _Res;
};

// Bare function type in template arg
template<typename _Functor>
struct _Weak_result_type_impl {};

template<typename _Res, typename... _ArgTypes>
struct _Weak_result_type_impl<_Res(_ArgTypes...)> {
    typedef _Res result_type;
};

// Function pointer type in template arg
template<typename _Res, typename... _ArgTypes>
struct _Weak_result_type_impl<_Res(*)(_ArgTypes...)> {
    typedef _Res result_type;
};

int main() {
    return 0;
}
