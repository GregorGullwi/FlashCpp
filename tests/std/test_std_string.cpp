// Test standard <string> header
#include <string>

int main() {
    std::string str = "Hello";
    std::string str2 = str + " World";
    
    return str.empty() ? 1 : 0;
}
