// Test copy constructor with different member sizes
struct AllSizes {
    char c = 5;
    short s = 10;
    int i = 20;
    long long ll = 40;
};

int main() {
    AllSizes a;  // Default constructor: c=5, s=10, i=20, ll=40
    AllSizes b = a;  // Copy constructor
    
    // Return sum: 5 + 10 + 20 + 40 = 75
    return b.c + b.s + b.i + b.ll;
}
