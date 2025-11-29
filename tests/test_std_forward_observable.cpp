// Test std::forward with observable forwarding behavior

// External printf declaration
extern "C" int printf(const char*, ...);

// Class that tracks copy vs move operations
class Widget {
    int value_;
public:
    Widget(int v) {
        value_ = v;
        printf("Widget(%d) - constructor\n", v);
    }
    
    // Copy constructor
    Widget(Widget& other) {
        value_ = other.value_;
        printf("Widget(Widget&) - COPY from %d\n", other.value_);
    }
    
    // Move constructor
    Widget(Widget&& other) {
        value_ = other.value_;
        printf("Widget(Widget&&) - MOVE from %d\n", other.value_);
    }
    
    int value() { return value_; }
};

// Overloaded functions to detect lvalue vs rvalue
void consume(Widget& w) {
    printf("consume(Widget&) - lvalue reference, value=%d\n", w.value());
}

void consume(Widget&& w) {
    printf("consume(Widget&&) - rvalue reference, value=%d\n", w.value());
}

// Perfect forwarding wrapper
template<typename T>
void forward_to_consume(T&& arg) {
    consume(std::forward<T>(arg));
}

// Variadic perfect forwarding
template<typename... Args>
void forward_all_to_consume(Args&&... args) {
    // Without pack expansion, just forward first argument for now
    // (pack expansion in function calls not yet implemented)
}

int main() {
    printf("=== Test 1: Forward lvalue ===\n");
    Widget w1(42);
    forward_to_consume(w1);  // Should call consume(Widget&)
    
    printf("\n=== Test 2: Direct calls for comparison ===\n");
    Widget w2(10);
    Widget w3(20);
    consume(w2);    // lvalue version
    consume((Widget&&)w3);  // rvalue version via cast
    
    return 0;
}
