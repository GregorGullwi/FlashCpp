// Test variadic non-type template parameters
template<size_t... _Indexes> 
struct _Index_tuple { };

// Test instantiation
using Test = _Index_tuple<0, 1, 2>;

int main() {
    return 0;
}
