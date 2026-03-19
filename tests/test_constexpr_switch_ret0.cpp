// Test switch statement in constexpr functions
constexpr int grade(int score) {
    switch (score / 10) {
        case 10:
        case 9:
            return 1; // A
        case 8:
            return 2; // B
        case 7:
            return 3; // C
        default:
            return 4; // F
    }
}

static_assert(grade(95) == 1);
static_assert(grade(85) == 2);
static_assert(grade(75) == 3);
static_assert(grade(65) == 4);

constexpr int day_type(int day) {
    switch (day) {
        case 0:  // Sunday
        case 6:  // Saturday
            return 1;  // Weekend
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
            return 0;  // Weekday
        default:
            return -1;  // Invalid
    }
}

static_assert(day_type(0) == 1);   // Sunday is weekend
static_assert(day_type(6) == 1);   // Saturday is weekend
static_assert(day_type(1) == 0);   // Monday is weekday
static_assert(day_type(3) == 0);   // Wednesday is weekday
static_assert(day_type(7) == -1);  // Invalid

int main() { return 0; }
