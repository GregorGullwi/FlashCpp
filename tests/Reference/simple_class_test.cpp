// Simple test for class instantiation and member calls

class Simple {
public:
    int value;
    
    void set(int x) {
        value = x;
    }
};

int main() {
    Simple s;
    s.set(42);
    return s.value;
}
