// Test basic variable template parsing

template<typename T>
constexpr T pi = T(3.14159265358979323846);

int main() {
    float pi_f = pi<float>;
    double pi_d = pi<double>;
    return 0;
}
