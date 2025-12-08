int main() {
    int value = 42;
    int* ptr = &value;
    
    // Reinterpret pointer as unsigned long long
    unsigned long long addr = reinterpret_cast<unsigned long long>(ptr);
    
    // Reinterpret back to pointer
    int* ptr2 = reinterpret_cast<int*>(addr);
    
    return *ptr2;
}
