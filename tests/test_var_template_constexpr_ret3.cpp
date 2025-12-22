template<typename T>
constexpr T pi = T(3.14159);

int main() {
    double x = pi<double>;
    int y = (int)pi<int>;  // Should be 3
    return y;
}
