// Test: pack expansion in return type via decltype
template<typename T, typename... Args>
auto make_and_sum(T first, Args... rest) -> decltype(first + (rest + ...)) {
return first + (rest + ...);
}

int main() {
return make_and_sum(10, 15, 17); // = 42
}
