// Test array in anonymous union
struct Container {
    int tag;
    union {
        int value;
        int arr[5];
    };
};

int main() {
    Container c;
    c.tag = 2;
    c.arr[0] = 1;
    c.arr[1] = 2;
    c.arr[2] = 3;
    c.arr[3] = 4;
    c.arr[4] = 0;  // arr[4] = 0
    
    // Sum first 4 elements: 1+2+3+4 = 10
    int sum = c.arr[0] + c.arr[1] + c.arr[2] + c.arr[3];
    return sum;
}
