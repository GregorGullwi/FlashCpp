int runtime_value() {
    return 42;
}

constinit int g = runtime_value();

int main() { return 0; }
