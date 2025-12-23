// Test standard <tuple> header
#include <tuple>

int main() {
    std::tuple<int, float, double> t(1, 2.0f, 3.0);
    auto val = std::get<0>(t);
    
    return val == 1 ? 0 : 1;
}
