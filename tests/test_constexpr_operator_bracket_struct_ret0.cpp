// Test that constexpr evaluation correctly dispatches operator[] on struct types,
// both at global scope and when called from inside constexpr functions.
// Previously, array subscript on a struct with operator[] produced
// "Array subscript on unsupported expression type" or
// "Subscript on non-array variable in constant expression".

// Case 1: global constexpr struct with pointer member
struct Str {
    const char* d;
    constexpr char operator[](int i) const { return d[i]; }
};
constexpr Str s{"hello"};
static_assert(s[0] == 'h');
static_assert(s[1] == 'e');
static_assert(s[4] == 'o');

// Case 2: operator[] on a local struct inside a constexpr function
constexpr char get_char() {
    Str s2{"world"};
    return s2[0];
}
static_assert(get_char() == 'w');

// Case 3: operator[] on a local struct with an int member
struct Wrap {
    int val;
    constexpr int operator[](int) const { return val; }
};

constexpr int wrap_get() {
    Wrap w{99};
    return w[0];
}
static_assert(wrap_get() == 99);

int main() { return 0; }
