// Test standard <any> header (C++17)
#include <any>

int main() {
    std::any a = 42;
    
    return std::any_cast<int>(a) == 42 ? 0 : 1;
}
