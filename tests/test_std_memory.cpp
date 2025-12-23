// Test standard <memory> header
#include <memory>

int main() {
    std::unique_ptr<int> ptr(new int(42));
    
    return *ptr == 42 ? 0 : 1;
}
