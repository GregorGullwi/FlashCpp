// Test for correct struct size calculation with nested templates
// Regression test for struct alignment bug where get_type_alignment
// was returning struct SIZE instead of ALIGNMENT

template<typename T> 
struct Level1 { 
    int a; 
    T b; 
    int c; 
};

template<typename T> 
struct Level2 { 
    int x; 
    T y; 
    int z; 
};

template<typename T> 
struct Level3 { 
    int p; 
    T q; 
    int r; 
};

int main() {
    // Test 3-level nesting - this was allocating wrong size before fix
    Level3<Level2<Level1<int>>> obj;
    obj.q.y.b = 42;
    return obj.q.y.b;
}
