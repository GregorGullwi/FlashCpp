// Complete variable template test

template<typename T>
constexpr T pi = T(3.14159265);

template<typename T>
T max_val = T(100);

int main() {
    // Basic instantiation
    float pi_f = pi<float>;
    double pi_d = pi<double>;
    
    int max_i = max_val<int>;
    
    return 0;
}
