int main() {
    int arr[5];
    
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    
    int x = arr[0];
    int y = arr[1];
    
    // Return 0 if sum is correct, 1 otherwise
    return (x + y == 30) ? 0 : 1;
}