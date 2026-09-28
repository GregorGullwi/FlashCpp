enum class ScopedState : unsigned char { active = 1 };

void consume(const unsigned char&);

int main() {
	consume(ScopedState::active);
}
