// Test standard <ranges> header (C++20)
#include <ranges>
#include <vector>

int main() {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    auto view = vec | std::views::take(3);
    
    return 0;
}
