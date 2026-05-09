#include <tuple>

int main() {
std::tuple<int> value(1);
return std::get<0>(value) == 1 ? 0 : 1;
}
