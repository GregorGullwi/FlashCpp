// Simple test without templates

int get_five() {
    return 5;
}

int get_ten() {
    return 10;
}

int main() {
    int a = get_five();
    int b = get_ten();
    
    // Should return 15
    return a + b;
}
