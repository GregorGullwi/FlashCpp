// Test: Pack expansion with actual member usage
template<typename... Args>
struct Tuple {
    Args... values;  // Expands to: T0 values0; T1 values1; etc.
};

int main() {
    Tuple<int> t1;
    t1.values0 = 42;
    
    Tuple<int, float> t2;
    t2.values0 = 10;
    t2.values1 = 3.14f;
    
    return t1.values0 + static_cast<int>(t2.values1) - 13;  // 42 + 3 - 13 = 32
}
