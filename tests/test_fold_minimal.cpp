// Minimal fold expression test

template<typename... Args>
int sum(Args... args) {
    return (... + args);
}

int main() {
    return sum(1, 2, 3);  // Should return 6
}
