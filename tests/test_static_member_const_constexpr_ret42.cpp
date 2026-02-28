struct Config {
    static constexpr int A = 20;
    static constexpr const int B = 22;
};

int main() {
    return Config::A + Config::B;
}
