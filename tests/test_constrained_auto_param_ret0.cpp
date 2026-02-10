// Test: C++20 constrained auto parameters (ConceptName auto param)
// The parser must handle concept-constrained auto parameter declarations

template<typename T>
concept IsInt = __is_same(T, int);

// Constrained auto parameters - parser must accept ConceptName auto param syntax
int identity(IsInt auto x) {
    return x;
}

int main() {
    return identity(0);
}
