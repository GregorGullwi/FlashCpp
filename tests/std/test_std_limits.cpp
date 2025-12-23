// Test standard <limits> header
#include <limits>

int main() {
    int max_int = std::numeric_limits<int>::max();
    float max_float = std::numeric_limits<float>::max();
    
    return max_int > 0 ? 0 : 1;
}
