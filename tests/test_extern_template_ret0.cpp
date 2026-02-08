// Test: extern template class declarations
// Verifies that 'extern template class T<args>;' is parsed correctly
// in both top-level and namespace scopes.

template<typename T>
class MyVec {
public:
    T val;
};

extern template class MyVec<int>;

namespace ns {
    template<typename T>
    class Box {
    public:
        T data;
    };
    extern template class Box<double>;
}

int main() {
    MyVec<int> v;
    v.val = 42;
    return v.val == 42 ? 0 : 1;
}
