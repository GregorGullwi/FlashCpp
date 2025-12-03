// Test: Pack expansion in member declarations
// Target: Args... members; should expand to multiple member variables

template<typename... Args>
struct Tuple {
    Args... values;  // Should expand to: T0 value0; T1 value1; etc.
};

int main() {
    Tuple<int> t1;           // Should have: int value0;
    Tuple<int, float> t2;    // Should have: int value0; float value1;
    
    return 0;
}
