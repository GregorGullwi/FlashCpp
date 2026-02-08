// Test: deduction guide with parameter pack expansion (_Up...)

template<typename _Tp, int _Nm>
struct array {
    _Tp data[_Nm];
};

template<typename _Tp, typename... _Up>
array(_Tp, _Up...) -> array<_Tp, 1 + sizeof...(_Up)>;

int main() {
    return 0;
}
