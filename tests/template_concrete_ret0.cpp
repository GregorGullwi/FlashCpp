// Test template with concrete types in signature
template<typename T>
int process(int x) { return x + 1; }

int main() {
    return process(-1);
}

