// Test: std::initializer_list<T> construction inside constexpr functions (C++20)
//
// C++20 [dcl.init.list] permits constexpr initializer_list usage.
// This test exercises the InitializerListConstructionNode path in the constexpr
// evaluator: when a constexpr function is called with a braced argument {1,2,3}
// whose parameter type is std::initializer_list<T>, the evaluator must synthesise
// a backing array and construct the begin/end pointer pair.
//
// Only size() (backed by pointer subtraction, already supported) is tested here;
// element iteration through a stored pointer is a separate known limitation.
//
// Expected exit code: 0

namespace std {
template <typename T>
class initializer_list {
public:
    const T* first_;
    const T* last_;

    constexpr initializer_list(const T* f, const T* l) noexcept : first_(f), last_(l) {}

    constexpr unsigned long size() const noexcept {
        return static_cast<unsigned long>(last_ - first_);
    }
};
} // namespace std

constexpr unsigned long count(std::initializer_list<int> lst) {
    return lst.size();
}

int main() {
    constexpr unsigned long n = count({1, 2, 3, 4, 5});
    static_assert(n == 5, "size of {1,2,3,4,5} should be 5");
    return static_cast<int>(n) - 5; // 0 on success
}
