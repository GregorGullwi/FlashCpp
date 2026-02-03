// Test comma-separated struct variable declarations
// Currently fails - returns 109 instead of 30
// This is a separate bug from the struct return size issue

struct Tiny {
    int value;
    Tiny(int v) : value(v) {}
    Tiny operator+(const Tiny& other) const {
        return Tiny(value + other.value);
    }
};

int main() {
    Tiny t1(10), t2(20);  // Declared together with comma
    Tiny tr = t1 + t2;
    return tr.value;  // Should be 30
}
