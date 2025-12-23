// Test standard <vector> header
#include <vector>

int main() {
    std::vector<int> vec;
    vec.push_back(42);
    vec.push_back(100);
    
    return vec.size() == 2 ? 0 : 1;
}
