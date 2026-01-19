// Minimal test case for forward enum class declaration parsing
// This only tests parsing, not usage

// Forward declaration of scoped enum with underlying type
enum class Status : int;

// Another forward declaration
enum class Color : unsigned char;

// Definition (without using the values)
enum class Status : int {
    Success = 0,
    Failure = 1
};

// Definition
enum class Color : unsigned char {
    Red = 0,
    Green = 1,
    Blue = 2
};

int main() {
    // Don't use the enum values - just test parsing
    return 0;
}
