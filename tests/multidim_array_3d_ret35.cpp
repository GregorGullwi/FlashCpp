// Test for 3D array basic access
// Expected return: 35

int main() {
    int arr[2][2][2];
    arr[0][0][0] = 1;
    arr[0][0][1] = 2;
    arr[0][1][0] = 3;
    arr[0][1][1] = 4;
    arr[1][0][0] = 5;
    arr[1][0][1] = 6;
    arr[1][1][0] = 7;
    arr[1][1][1] = 7;  // Different value to make sum not symmetric
    
    // Read corners and edges
    int a = arr[0][0][0];  // 1
    int b = arr[1][1][1];  // 7
    int c = arr[0][1][1];  // 4
    int d = arr[1][0][0];  // 5
    
    // More reads to test different indices
    int e = arr[0][0][1];  // 2
    int f = arr[1][1][0];  // 7
    int g = arr[0][1][0];  // 3
    int h = arr[1][0][1];  // 6
    
    return a + b + c + d + e + f + g + h;  // 1+7+4+5+2+7+3+6 = 35
}
