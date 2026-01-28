// Test template specialization parsing

template <typename _Ty>
struct test_struct {
    int value;
};

// Partial specialization with reference
template <typename _Ty>
struct test_struct<_Ty&> {
    int value;
};

int main() {
    test_struct<int> a;
    test_struct<int&> b;
    return 0;
}
