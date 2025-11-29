// Test that constinit rejects non-constant initializers

int runtime_value() {
    return 42;
}

// This should fail if constinit enforcement is working
constinit int should_fail = runtime_value();

int main() {
    return 0;
}
