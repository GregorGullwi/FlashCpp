struct MixedDesignated {
	int x;
	int y;
};

constexpr MixedDesignated value = {1, .y = 2};

int main() {
	return value.x + value.y;
}
