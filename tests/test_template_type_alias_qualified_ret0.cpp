// Test: qualified name with type alias in template struct body
// In a template struct, "pointer::member()" where pointer is a using-alias
// should be accepted as a dependent expression
template<typename _Ptr, typename _Elt>
struct ptr_traits {
    using pointer = _Ptr;
    using element_type = _Elt;
    
    // pointer::pointer_to(__r) is a dependent expression (pointer = _Ptr is template-dependent)
    static pointer pointer_to(element_type& __r)
    { return pointer::pointer_to(__r); }
};

struct MyPtr {
    using element_type = int;
    int val;
    static MyPtr pointer_to(int& r) { return MyPtr{r}; }
};

int main() {
    // Don't actually instantiate - just verify the template parses
    return 0;
}
