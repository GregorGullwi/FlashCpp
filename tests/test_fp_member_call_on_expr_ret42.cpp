// Test: function-pointer member called on a temporary expression result
// e.g., getContainer().callback(40, 2) should return 42

struct Container {
int (*callback)(int, int);
};

int add(int a, int b) { return a + b; }

Container getContainer() {
return Container{add};
}

int main() {
int result = getContainer().callback(40, 2);
return result;
}
