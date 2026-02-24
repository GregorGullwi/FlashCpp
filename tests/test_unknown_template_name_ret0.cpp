// Test that template names containing "unknown" don't collide with internal markers.
// This verifies Phase 4 of the hash-based name refactoring:
// the compiler no longer uses "_unknown" as a sentinel in type names.

template<typename T>
struct is_unknown {
    static constexpr bool value = false;
};

template<>
struct is_unknown<int> {
    static constexpr bool value = true;
};

template<typename T>
struct has_unknown_member {
    T data;
    int unknown;
};

int main() {
    // Verify template with "unknown" in name works correctly
    bool v1 = is_unknown<int>::value;   // true
    bool v2 = is_unknown<float>::value; // false

    has_unknown_member<int> obj;
    obj.data = 42;
    obj.unknown = 99;

    if (!v1) return 1;
    if (v2) return 2;
    if (obj.data != 42) return 3;
    if (obj.unknown != 99) return 4;

    return 0;
}
