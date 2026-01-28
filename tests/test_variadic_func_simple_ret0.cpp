// Test: Simple variadic function template (just parsing for now)
template<typename... Args>
void print(Args... args);  // Pack expansion in parameters

int main() {
    return 0;
}
