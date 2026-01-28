// Test variable template parsing (no instantiation yet)

template<typename T>
constexpr T pi = T(3.14159265358979323846);

template<typename T>
inline T max_value = T(100);

// Just a simple main to make it a valid program
int main() {
    return 0;
}
