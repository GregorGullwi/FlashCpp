// Test pointer addition with long* (element size = 8)
int main() {
    long arr[6] = {100, 200, 300, 400, 500, 600};
    long* p = arr;
    long* q = p + 2;  // q points to arr[2]
    
    long diff = q - p;  // Should be 2
    return (int)diff;
}
