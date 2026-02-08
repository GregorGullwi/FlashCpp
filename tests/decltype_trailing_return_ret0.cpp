// Test: decltype with pack expansion in trailing return type
// Validates: -> decltype(func(args...)) syntax in template context

template<typename _Alloc>
struct allocator_traits {
    template<typename _Tp, typename... _Args>
    static void _S_construct(_Alloc& __a, _Tp* __p, _Args&&... __args);

    template<typename _Tp, typename... _Args>
    static constexpr auto
    construct(_Alloc& __a, _Tp* __p, _Args&&... __args)
        -> decltype(_S_construct(__a, __p, __args...))
    { _S_construct(__a, __p, __args...); }
};

int main() {
    return 0;
}
