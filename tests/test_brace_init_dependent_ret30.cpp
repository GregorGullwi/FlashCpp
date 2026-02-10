// Test: brace-init return for dependent types in template bodies
// This tests that return { expr1, expr2 } works when the return type
// is a dependent UserDefined type in a template body.

struct Pair {
    int first;
    int second;
};

template<typename T>
struct Container {
    using value_type = Pair;
    
    value_type make_pair(int a, int b) {
        return { a, b };
    }
};

int main() {
    Container<int> c;
    Pair p = c.make_pair(10, 20);
    return p.first + p.second; // 30
}
