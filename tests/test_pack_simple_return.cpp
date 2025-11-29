// Test: Simpler pack expansion test
template<typename... Args>
struct Tuple {
    Args... values;
};

int main() {
    Tuple<int> t;
    t.values0 = 5;
    return t.values0;  // Should return 5
}
