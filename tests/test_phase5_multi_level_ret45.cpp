// Phase 5 comprehensive test: Multiple levels of template nesting with >> splitting
// Tests maximal munch rule for >> in various scenarios

template<typename T>
struct Level1 {
    T value;
};

template<typename T>
struct Level2 {
    T inner;
};

template<typename T>
struct Level3 {
    T data;
};

int main() {
    // Test 1: Double nesting
    Level2<Level1<int>> double_nested;
    double_nested.inner.value = 10;
    
    // Test 2: Triple nesting - this requires splitting >>>
    // Note: >>> will be lexed as >> followed by >, not split by our code
    Level3<Level2<Level1<int>>> triple_nested;
    triple_nested.data.inner.value = 20;
    
    // Test 3: Multiple separate nested templates
    Level1<Level1<int>> nested1;
    Level1<Level1<int>> nested2;
    nested1.value.value = 5;
    nested2.value.value = 10;
    
    int sum = double_nested.inner.value + triple_nested.data.inner.value;
    sum += nested1.value.value + nested2.value.value;
    
    return sum;  // 10 + 20 + 5 + 10 = 45
}
