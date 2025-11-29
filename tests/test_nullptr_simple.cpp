// Simple nullptr test

int* getNull() {
    return nullptr;
}

int main() {
    int* p = getNull();
    if (p == nullptr) {
        return 0;
    }
    return 1;
}
