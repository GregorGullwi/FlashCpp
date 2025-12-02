int test() {
    char* args;
    int value = (*(int*)((args += 8) - 8));
    return value;
}

int main() {
    return 0;
}
