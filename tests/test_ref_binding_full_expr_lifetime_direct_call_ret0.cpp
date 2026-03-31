int g_stage = 0;

struct Watch {
	int value;

	Watch(int v) : value(v) {
		g_stage = 1;
	}

	~Watch() {
		g_stage += 10;
	}
};

int use(const Watch& w) {
	return w.value * 100 + g_stage;
}

int main() {
	int result = use(Watch(4));
	if (result != 401)
		return 1;
	if (g_stage != 11)
		return 2;
	return 0;
}
