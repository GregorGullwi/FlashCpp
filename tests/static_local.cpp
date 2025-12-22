// Test static local variables

int counter() {
    static int count = 0;
    count = count + 1;
    return count;
}

int get_static_value() {
    static int value = 42;
    return value;
}

int uninitialized_static() {
    static int x;
    return x;
}


int main() {
    return counter();
}
