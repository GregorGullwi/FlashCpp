// Test returning SimpleOrdering from a member function
struct SimpleOrdering {
    int value;
    
    SimpleOrdering(int v) : value(v) {}
};

struct Foo {
    SimpleOrdering makeOrdering(int v) {
        return SimpleOrdering(v);
    }
};

int main() {
    Foo f;
    SimpleOrdering result = f.makeOrdering(-1);
    return result.value;
}
