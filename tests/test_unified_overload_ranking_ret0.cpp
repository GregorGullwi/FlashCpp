// Test C++20 [over.match.oper]/2 unified candidate ranking:
// Both member and free-function candidates must be ranked together.
//
// struct A has member operator+(int), which requires a UserDefined
// conversion on the RHS when called as a + a (A → int).
// The free function operator+(const A&, const A&) is an ExactMatch on both.
// Correct: a + a selects the free function (ExactMatch > UserDefined).

struct A {
    int value;

    // Member operator+ that takes int (not A)
    A operator+(int rhs) {
        return A{value + rhs};
    }
};

// Free-function operator+ that takes (const A&, const A&) — ExactMatch on both operands
A operator+(const A& lhs, const A& rhs) {
    return A{lhs.value + rhs.value};
}

int main() {
    A a{10};
    A b{20};
    A result = a + b;  // Should call free operator+(A, A), not member A::operator+(int)
    if (result.value == 30) {
        return 0;  // success: free function was called (ExactMatch)
    }
    return 1;  // failure: member was selected (UserDefined conversion)
}
