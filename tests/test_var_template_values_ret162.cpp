// Test variable template with constexpr initializers - verify actual values
template<typename T>
constexpr T pi = T(3.14159265);

template<typename T>
constexpr T max_val = T(100);

int main() {
    // Test that values are actually initialized (not zero)
    // pi<float> should be ~3.14, so multiply by 10 and truncate to int
    float pi_f = pi<float>;
    int pi_times_10 = (int)(pi_f * 10.0f);  // Should be 31
    
    // pi<double> similarly
    double pi_d = pi<double>;
    int pi_d_times_10 = (int)(pi_d * 10.0);  // Should be 31
    
    // max_val<int> should be 100
    int max_i = max_val<int>;
    
    // Return value that depends on all three being correctly initialized
    // If zero-initialized: 0 + 0 + 0 = 0
    // If correct: 31 + 31 + 100 = 162
    return pi_times_10 + pi_d_times_10 + max_i;
}
