// Test file for abbreviated function templates (C++20)
// Note: This is a simplified test - full support for concept auto parameters
// requires significant parser changes

// Simple abbreviated function template using auto (without concept)
void display(auto value) {
    // Function body - simplified
}

int main() {
    display(100);
    return 0;
}
