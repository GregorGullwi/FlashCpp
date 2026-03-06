template<typename T>
int increment() { static int count = 0; return ++count; }
int main() {
    increment<int>();
    return increment<int>() - 1;
}
