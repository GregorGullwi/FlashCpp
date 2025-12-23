// Test standard <variant> header (C++17)
#include <variant>

int main() {
    std::variant<int, float> v = 42;
    
    return std::get<int>(v) == 42 ? 0 : 1;
}
