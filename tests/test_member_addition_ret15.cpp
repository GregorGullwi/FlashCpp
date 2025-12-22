// Test member functions without templates

struct Simple {
    int get_five() {
        return 5;
    }
    
    int get_ten() {
        return 10;
    }
};

int main() {
    Simple s;
    int a = s.get_five();
    int b = s.get_ten();
    
    // Should return 15
    return a + b;
}
