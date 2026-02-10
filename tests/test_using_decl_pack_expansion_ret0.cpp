// Test: using-declaration with pack expansion (C++17)
// Pattern: using Base<Args>::member...;

template<int I>
struct Fun {
    static int call() { return I; }
};

template<typename... Bases>
struct Combined : Bases... {
    using Bases::call...;
};

int main() {
    Combined<Fun<0>> c;
    return c.call(); // 0
}
