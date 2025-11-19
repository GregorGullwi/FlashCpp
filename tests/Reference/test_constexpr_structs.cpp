// Test constexpr structs, classes, and statics

struct Point {
    int x;
    int y;

    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}

    constexpr int sum() const {
        return x + y;
    }

    static constexpr int static_sum(int a, int b) {
        return a + b;
    }
};

constexpr Point p1(10, 20);
static_assert(p1.x == 10, "p1.x should be 10");
static_assert(p1.y == 20, "p1.y should be 20");
static_assert(p1.sum() == 30, "p1.sum() should be 30");
static_assert(Point::static_sum(5, 5) == 10, "Point::static_sum should be 10");

// Global statics
static constexpr int global_static_val = 100;
static_assert(global_static_val == 100, "global_static_val should be 100");

// Template statics
template <typename T>
struct Math {
    static constexpr T pi = T(3); // Simplified pi
    
    static constexpr T square(T val) {
        return val * val;
    }
};

static_assert(Math<int>::pi == 3, "Math<int>::pi should be 3");
static_assert(Math<int>::square(4) == 16, "Math<int>::square(4) should be 16");

// Variable templates
template <typename T>
constexpr T zero = T(0);

static_assert(zero<int> == 0, "zero<int> should be 0");
static_assert(zero<double> == 0.0, "zero<double> should be 0.0");
