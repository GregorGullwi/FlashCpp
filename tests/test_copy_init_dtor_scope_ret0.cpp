int g_dtor_sum = 0;

struct Tracked {
	int id;

	Tracked(int value) : id(value) {}
	Tracked(const Tracked& other) : id(other.id + 10) {}
	~Tracked() { g_dtor_sum += id; }
};

int main() {
	{
		Tracked source(1);
		{
			Tracked copy = source;
		}
		if (g_dtor_sum != 11) {
			return 1;
		}
	}
	return g_dtor_sum == 12 ? 0 : 2;
}
