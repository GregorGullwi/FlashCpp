// Test: using type alias with array dimensions
// Validates: using _Type = _Tp[_Nm]; syntax

template<typename _Tp, int _Nm>
struct array_wrapper {
    using _Type = _Tp[_Nm];
    static constexpr int size = _Nm;
};

int main() {
    return array_wrapper<int, 4>::size;
}
