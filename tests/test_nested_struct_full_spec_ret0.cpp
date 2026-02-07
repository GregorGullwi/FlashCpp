// Test: Nested struct declarations inside full template specializations
// Verifies that struct/class declarations are handled in template<> struct bodies.

template<typename T>
struct Promise {
    T value;
};

struct MyPromise {};

template<>
struct Promise<MyPromise> {
    struct Frame {
        int x;
        int y;
    };
    
    static constexpr int answer{42};
    
    int get() { return answer; }
};

int main() {
    Promise<MyPromise> p;
    return p.get() == 42 ? 0 : 1;
}
