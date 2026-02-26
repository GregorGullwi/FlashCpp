// Test: PackExpansionExprNode in function call contexts
// Pattern: func(transform(args)...) where args is a function parameter pack
int add(int a, int b, int c) { return a + b + c; }
int identity(int x) { return x; }

template<typename... Args>
int transform_and_add(Args... args) {
    return add(identity(args)...);
}

int main() {
    return transform_and_add(10, 20, 12);  // identity(10) + identity(20) + identity(12) = 42
}
