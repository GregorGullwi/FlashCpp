// Test standard <functional> header
#include <functional>

int add(int a, int b) {
    return a + b;
}

int main() {
    std::function<int(int, int)> func = add;
    int result = func(3, 4);
    
    return result == 7 ? 0 : 1;
}
