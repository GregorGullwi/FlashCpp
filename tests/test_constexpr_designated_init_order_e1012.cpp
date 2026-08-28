struct OrderedDesignated {
	int x;
	int y;
	int z;
};

constexpr OrderedDesignated value = {.y = 2, .x = 1};

int main() {
	return value.x + value.y + value.z;
}
