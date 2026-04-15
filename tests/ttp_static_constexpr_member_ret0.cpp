// Test: Static constexpr member initializer accessing member via TTP instantiation
// This should return 0 when working correctly

template <typename T>
struct box { static constexpr int id = sizeof(T); };

template <template <typename> class W>
struct probe {
    static constexpr int sz = W<int>::id;  // Access static member via TTP instantiation
};

int main() { return probe<box>::sz - 4; }  // box<int>::id == sizeof(int) == 4
