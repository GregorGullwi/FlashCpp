// Test constexpr features

// Test 1: Constexpr constructors and methods
struct Point {
    int x, y;
    
    constexpr Point(int x_, int y_) : x(x_), y(y_) {}
    
    constexpr int sum() const { return x + y; }
};

constexpr Point p(3, 4);
static_assert(p.sum() == 7, "constexpr constructor and method failed");

// Test 2: Constexpr with more complex logic
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial(5) == 120, "constexpr factorial failed");

// Test 3: Constexpr with conditionals
constexpr int max(int a, int b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

static_assert(max(10, 5) == 10, "constexpr max failed");

// Test 4: Constexpr with arrays
constexpr int sum_array(const int* arr, int size) {
    int sum = 0;
    for (int i = 0; i < size; ++i) {
        sum += arr[i];
    }
    return sum;
}

constexpr int arr[] = {1, 2, 3, 4, 5};
static_assert(sum_array(arr, 5) == 15, "constexpr array sum failed");

int main() {
    return 42;
}
