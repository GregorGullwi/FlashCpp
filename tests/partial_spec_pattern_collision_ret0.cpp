// Test that templates with "_pattern" in their name don't collide
// with the internal pattern naming used for partial specializations.

// ============================================================
// 1. Basic collision test: template name contains "_pattern"
// ============================================================
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

// ============================================================
// 2. Friend class access through partial specialization patterns
//    Exercises checkFriendClassAccess step 4 (registry lookup).
// ============================================================
template<typename T>
struct Container;

class Secret {
    int value;
    // Grant friend access to the Container template
    friend struct Container<int>;
    friend struct Container<int*>;
public:
    Secret(int v) : value(v) {}
};

template<typename T>
struct Container {
    T data;
    // Access private member of Secret - requires friend resolution
    int read_secret(Secret& s) { return s.value; }
};

// Partial specialization for pointers - also needs friend access
template<typename T>
struct Container<T*> {
    T* data;
    int read_secret(Secret& s) { return s.value + 10; }
};

// ============================================================
// 3. Member struct template with partial specializations
//    Tests the nested/qualified pattern name path
//    (ParentClass::InnerTemplate$pattern_...)
// ============================================================
struct Outer {
    template<typename T>
    struct Inner {
        T val;
        int id() { return 100; }
    };

    // Partial specialization of a member struct template
    template<typename T>
    struct Inner<T*> {
        T* val;
        int id() { return 200; }
    };
};

// ============================================================
// 4. Another "_pattern" collision: template name is exactly "_pattern"
//    Stress test: the user name is a substring of the old separator.
// ============================================================
template<typename T>
struct _pattern {
    T x;
    int tag() { return 50; }
};

template<typename T>
struct _pattern<T*> {
    T* x;
    int tag() { return 60; }
};

int main() {
    int result = 0;

    // --- Test 1: basic collision ---
    my_pattern_matcher<int> a{42};
    int x = 10;
    my_pattern_matcher<int*> b{&x};
    my_pattern_matcher<int&> c{x};

    // Primary: 1, pointer partial spec: 2, reference partial spec: 3
    if (a.get() != 1) result |= 1;
    if (b.get() != 2) result |= 2;
    if (c.get() != 3) result |= 4;

    // --- Test 2: friend access through partial spec ---
    Secret secret{42};
    Container<int> ci{7};
    if (ci.read_secret(secret) != 42) result |= 8;

    Container<int*> cp{&x};
    if (cp.read_secret(secret) != 52) result |= 16;  // 42 + 10

    // --- Test 3: member struct template partial spec ---
    Outer::Inner<int> inner_val{5};
    if (inner_val.id() != 100) result |= 32;

    Outer::Inner<int*> inner_ptr{&x};
    if (inner_ptr.id() != 200) result |= 64;

    // --- Test 4: template named "_pattern" ---
    _pattern<int> p1{99};
    if (p1.tag() != 50) result |= 128;

    _pattern<int*> p2{&x};
    if (p2.tag() != 60) result |= 256;

    // All bits clear → return 0
    return result;
}
