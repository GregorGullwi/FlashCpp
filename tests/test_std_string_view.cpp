// Test standard <string_view> header
#include <string_view>

int main() {
    std::string_view sv = "Hello, World!";
    std::string_view sv2 = sv;
    
    return sv.empty() ? 1 : 0;
}
