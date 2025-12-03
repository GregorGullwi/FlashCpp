// Test improved constexpr evaluation

// ===== UNARY MINUS AND PLUS =====
constexpr int negative_val = -42;
static_assert(negative_val == -42, "unary minus should work");

constexpr int positive_val = +42;
static_assert(positive_val == 42, "unary plus should work");

constexpr int double_negative = -(-10);
static_assert(double_negative == 10, "double negative should work");

// ===== COMPLEX MEMBER INITIALIZER EXPRESSIONS =====
struct Rectangle {
    int width;
    int height;
    int area;
    
    constexpr Rectangle(int w, int h) : width(w), height(h), area(w * h) {}
};

constexpr Rectangle r(10, 20);
static_assert(r.width == 10, "r.width should be 10");
static_assert(r.height == 20, "r.height should be 20");
static_assert(r.area == 200, "r.area should be 200 (computed from w * h)");

// More complex initializer expressions
struct Complex {
    int a;
    int b;
    int sum;
    int diff;
    int product;
    
    constexpr Complex(int x, int y) 
        : a(x), b(y), sum(x + y), diff(x - y), product(x * y) {}
};

constexpr Complex c(15, 5);
static_assert(c.a == 15, "c.a should be 15");
static_assert(c.b == 5, "c.b should be 5");
static_assert(c.sum == 20, "c.sum should be 20");
static_assert(c.diff == 10, "c.diff should be 10");
static_assert(c.product == 75, "c.product should be 75");

// ===== DEFAULT MEMBER INITIALIZERS =====
struct Config {
    int timeout = 30;
    int retries = 5;
    int max_connections;
    
    constexpr Config(int max_conn) : max_connections(max_conn) {}
};

constexpr Config cfg(100);
static_assert(cfg.timeout == 30, "cfg.timeout should use default value 30");
static_assert(cfg.retries == 5, "cfg.retries should use default value 5");
static_assert(cfg.max_connections == 100, "cfg.max_connections should be 100");

// ===== CONSTEXPR MEMBER FUNCTIONS =====
struct Point {
    int x;
    int y;
    
    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}
    
    constexpr int sum() const {
        return x + y;
    }
    
    constexpr int diff() const {
        return x - y;
    }
    
    constexpr int product() const {
        return x * y;
    }
};

constexpr Point p1(10, 20);
static_assert(p1.x == 10, "p1.x should be 10");
static_assert(p1.y == 20, "p1.y should be 20");
static_assert(p1.sum() == 30, "p1.sum() should be 30");
static_assert(p1.diff() == -10, "p1.diff() should be -10");
static_assert(p1.product() == 200, "p1.product() should be 200");

// Member function with more complex expression
struct Calculator {
    int value;
    
    constexpr Calculator(int v) : value(v) {}
    
    constexpr int doubled() const {
        return value * 2;
    }
    
    constexpr int squared() const {
        return value * value;
    }
};

constexpr Calculator calc(7);
static_assert(calc.value == 7, "calc.value should be 7");
static_assert(calc.doubled() == 14, "calc.doubled() should be 14");
static_assert(calc.squared() == 49, "calc.squared() should be 49");

int main() {
    // If this compiles and links, all constexpr tests passed!
    return 0;
}
