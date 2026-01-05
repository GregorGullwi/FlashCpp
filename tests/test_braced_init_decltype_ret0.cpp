// Test braced initializer in decltype expression
// This pattern appears in <type_traits> line 1326:
//   decltype(__helper<const _Tp&>({}))

void helper(const int&) {}

// Use braced initializer {} in decltype context
template<typename T>
using result_type = decltype(helper({}));

int main() {
    // The test is about compilation, not execution
    return 0;
}
