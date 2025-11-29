// Test variable template with constexpr initializers - verify actual values
template<typename T>
constexpr T pi = T(3.14159265);

template<typename T>
constexpr T max_val = T(100);

int main() {
    // Test that values are actually initialized (not zero)
    // pi<float> should be ~3.14, so multiply by 100 and truncate to int
    float pi_f = pi<float>;
    int pi_times_100 = (int)(pi_f * 100.0f);  // Should be 314
    
    // pi<double> similarly
    double pi_d = pi<double>;
    int pi_d_times_100 = (int)(pi_d * 100.0);  // Should be 314
    
    // max_val<int> should be 100
    int max_i = max_val<int>;
    
    // Return value that depends on all three being correctly initialized
    // If zero-initialized: 0 + 0 + 0 = 0
    // If correct: 314 + 314 + 100 = 728
    return pi_times_100 + pi_d_times_100 + max_i;
}
