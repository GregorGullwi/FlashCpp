// Test typedef with reference declarators inside template class body
// This pattern is commonly used in standard library headers like <initializer_list>

template<class E>
class test_container {
public:
    typedef E value_type;
    typedef const E& const_reference;
    typedef E& reference;
    typedef E&& rvalue_reference;
    typedef const E* const_pointer;
    typedef E* pointer;
};

int main() {
    test_container<int> t;
    return 0;
}
