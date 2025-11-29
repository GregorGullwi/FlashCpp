// Test single variable template return
template<typename T>
static constexpr T test_val = T(99);

int main() {
    return test_val<int>;  // Should be 99
}
