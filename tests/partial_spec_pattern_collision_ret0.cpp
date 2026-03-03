// Test that templates with "_pattern" in their name don't collide
// with the internal pattern naming used for partial specializations.
template<typename T>
struct my_pattern_matcher {
    T value;
    int get() { return 1; }
};

// Partial specialization for pointers - should get unique internal name
template<typename T>
struct my_pattern_matcher<T*> {
    T* value;
    int get() { return 2; }
};

// Partial specialization for references
template<typename T>
struct my_pattern_matcher<T&> {
    T& value;
    int get() { return 3; }
};

int main() {
    my_pattern_matcher<int> a{42};
    int x = 10;
    my_pattern_matcher<int*> b{&x};
    my_pattern_matcher<int&> c{x};
    
    // Primary: 1, pointer partial spec: 2, reference partial spec: 3
    int result = 0;
    if (a.get() != 1) result |= 1;
    if (b.get() != 2) result |= 2;
    if (c.get() != 3) result |= 4;
    return result;
}
