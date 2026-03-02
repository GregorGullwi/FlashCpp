// Test constexpr member functions that modify this->member (Item 8, Members.cpp:202)
// The fix enables this->member assignment in constexpr member functions
struct Counter {
    int x = 0;
    constexpr void set(int v) { this->x = v; }
    constexpr int get() const { return this->x; }
};

constexpr int compute() {
    Counter c;
    c.set(42);
    return c.get();
}

int main() {
    return compute();
}
