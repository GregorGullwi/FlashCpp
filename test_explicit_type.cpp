// Test explicit lambda type
struct __lambda_0 {};  // Placeholder

struct Test {
    __lambda_0 get_lambda();  // Explicit return type
};

int main() {
    Test obj;
    __lambda_0 lambda = obj.get_lambda();
    // Can't call lambda() because __lambda_0 is just a placeholder
    return 5;
}
