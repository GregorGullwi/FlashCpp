// Test: template brace-initialization in return statements
// return Type<Args>{val1, val2} must work

template<typename T>
struct Pair {
    T first;
    T second;
};

Pair<int> make_pair(int a, int b) {
    return Pair<int>{a, b};
}

int main() {
    auto p = make_pair(3, 7);
    return p.first + p.second == 10 ? 0 : 1;
}
