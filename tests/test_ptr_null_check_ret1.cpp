// Test that pointer null comparison works correctly
int check_null(int* ptr) {
    if (ptr == 0) return 1;
    return 0;
}

int main() {
    return check_null(0);
}
