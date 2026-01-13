// Test globally qualified new expression (::new)
// Pattern: (void) ::new T - creates an object using global operator new

int main() {
    int* p = ::new int(42);  // Global scope operator new
    int result = *p;
    ::delete p;
    return result == 42 ? 0 : 1;
}
