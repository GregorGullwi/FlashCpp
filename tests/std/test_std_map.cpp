// Test standard <map> header
#include <map>

int main() {
    std::map<int, int> m;
    m[1] = 100;
    m[2] = 200;
    
    return m[1] == 100 ? 0 : 1;
}
