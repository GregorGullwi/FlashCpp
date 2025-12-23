// Test standard <set> header
#include <set>

int main() {
    std::set<int> s;
    s.insert(1);
    s.insert(2);
    s.insert(3);
    
    return s.size() == 3 ? 0 : 1;
}
