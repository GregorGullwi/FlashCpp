// Test standard <chrono> header
#include <chrono>

int main() {
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::seconds(5);
    
    return 0;
}
