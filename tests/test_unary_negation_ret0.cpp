// Test unary negation operator in template base class expressions

template<int N>
struct int_constant {
    static constexpr int value = N;
};

template<typename T>
struct Num {
    static constexpr int num = 5;
};

// Test with unary negation operator in template argument
template<typename T>
struct negated_num : int_constant<-Num<T>::num> { };

int main() {
    // negated_num<int>::value should be -5, return 0 if correct
    return negated_num<int>::value == -5 ? 0 : 1;
}