// Minimal test case for forward enum class declaration support
// This tests if FlashCpp can handle forward declarations of scoped enums

// Forward declaration of scoped enum with underlying type
enum class Status : int;

// Another forward declaration
enum class Color : unsigned char;

// Definition
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

// Test usage
int test_forward_enum() {
    Status s = Status::Success;
    Color c = Color::Red;
    
    if (s == Status::Success && c == Color::Red) {
        return 0;
    }
    return 1;
}

int main() {
    return test_forward_enum();
}
