// Test switch statement in constexpr member functions
struct Classifier {
    int value;

    constexpr Classifier(int v) : value(v) {}

    constexpr int classify() const {
        switch (value) {
            case 0:
                return 0;
            case 1:
            case 2:
                return 1;
            default:
                return 2;
        }
    }
};

constexpr Classifier c0(0);
constexpr Classifier c1(1);
constexpr Classifier c2(2);
constexpr Classifier c5(5);

static_assert(c0.classify() == 0);
static_assert(c1.classify() == 1);
static_assert(c2.classify() == 1);
static_assert(c5.classify() == 2);

int main() { return 0; }
