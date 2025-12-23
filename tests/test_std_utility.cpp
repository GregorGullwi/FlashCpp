// Test standard <utility> header
#include <utility>

int main() {
    std::pair<int, float> p(42, 3.14f);
    
    return p.first == 42 ? 0 : 1;
}
