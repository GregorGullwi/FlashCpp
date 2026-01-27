int main() {
    int numbers[10];
    numbers[0] = 100;
    numbers[5] = 500;
    int first = numbers[0];
    int middle = numbers[5];
    // Return 0 if sum is correct, 1 otherwise
    return (first + middle == 600) ? 0 : 1;
}