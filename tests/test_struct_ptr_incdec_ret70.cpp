struct P {
	int x{};
};

int main() {
	P arr[3]{};
	arr[0].x = 10;
	arr[1].x = 20;
	arr[2].x = 30;

	P* p = &arr[0];
	++p;
	int prefix = p->x;
	P* old = p++;

	return prefix + old->x + p->x;
}