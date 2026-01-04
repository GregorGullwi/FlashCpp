// Test pointer addition with short* (element size = 2)
int main() {
    short arr[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    short* p = arr;
    short* q = p + 4;  // q points to arr[4]
    
    long diff = q - p;  // Should be 4
    return (int)diff;
}
