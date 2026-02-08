// Test: [[likely]]/[[unlikely]] attributes after if conditions
int classify(int x) {
    if (x > 0) [[likely]]
        return 42;
    else [[unlikely]]
        return 0;
}

int main() {
    return classify(1);
}
