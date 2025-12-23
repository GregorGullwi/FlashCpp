// Test standard <optional> header (C++17)
#include <optional>

int main() {
    std::optional<int> opt = 42;
    
    return opt.has_value() ? 0 : 1;
}
