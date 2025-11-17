// Test function pointer with nullptr (non-member)

int (*getFuncPtr())() {
    return nullptr;
}

int main() {
    int (*fp)() = getFuncPtr();
    if (fp == nullptr) {
        return 0;
    }
    return 1;
}
