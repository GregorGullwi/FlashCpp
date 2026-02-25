// Test: static member functions in template classes
template<typename T>
struct Calculator {
    static T add(T a, T b) {
        return a + b;
    }
    
    static T multiply(T a, T b) {
        return a * b;
    }
};

int main() {
    int sum = Calculator<int>::add(10, 20);
    int prod = Calculator<int>::multiply(3, 4);
    return (sum == 30 && prod == 12) ? 42 : 0;
}
