// Test that block-scope static variable retains static storage duration
// A local static counter should persist across function calls
// Expected return: 3

int count() {
    static int counter = 0;
    counter++;
    return counter;
}

int main() {
    count();   // counter = 1
    count();   // counter = 2
    return count();  // counter = 3
}
