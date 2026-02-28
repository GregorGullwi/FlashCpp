// Regression test: pack expansion ... in qualified member function call arguments
// The parser must handle trailing ... pack expansion in function call argument lists

template<typename... Args>
int countArgs(Args... args) {
    return sizeof...(args);
}

template<typename... Args>
int forwardCount(Args... args) {
    return countArgs(args...);  // pack expansion in function call
}

int main() {
    return forwardCount(1, 2, 3) - 3;  // 0
}
