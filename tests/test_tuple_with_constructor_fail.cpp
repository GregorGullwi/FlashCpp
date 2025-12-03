// Test: Variadic tuple with constructor (simplified - no pack expansion in params yet)
template<typename... Args>
struct Tuple {
    Args... values;
    
    // For now, use explicit constructors for each instantiation
    // Full pack expansion in constructors would come later
};

// Explicit specialization for Tuple<int>
template<>
struct Tuple<int> {
    int values0;
    
    Tuple(int v0) : values0(v0) {}
};

// Explicit specialization for Tuple<int, float>
template<>
struct Tuple<int, float> {
    int values0;
    float values1;
    
    Tuple(int v0, float v1) : values0(v0), values1(v1) {}
};

int main() {
    Tuple<int> t1(42);
    Tuple<int, float> t2(10, 3.14f);
    
    return t1.values0 + static_cast<int>(t2.values1) - 45;  // 42 + 3 - 45 = 0
}
