// Test: Out-of-line class-template member function body replay preserves
// definition-context lookup for non-dependent unqualified names.
// Two-phase lookup requires that non-dependent names bind at the definition
// point, not at the point of instantiation.

int pick(long) { return 11; }

template<typename T>
struct Box { 
    int run(); 
};

template<typename T>
int Box<T>::run() { 
    return pick(0);  // Non-dependent call: should bind to pick(long) at definition point
}

int pick(int) { return 22; }  // Declared after definition, should not be considered

int main() { 
    return Box<int>{}.run() == 11 ? 0 : 1; 
}
