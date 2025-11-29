// Test nullptr comparison without structs

int main() {
    int* p = nullptr;
    int result = (p == nullptr) ? 1 : 0;
    return result;
}
