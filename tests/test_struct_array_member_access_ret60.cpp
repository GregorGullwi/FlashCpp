struct P
{
	int x;
};

int main() {
	P p[3];
	p[0].x = 10;
	p[1].x = 20;
	p[2].x = 30;
	int sum = 0;
	for (int i = 0; i < 3; ++i) {
		sum = sum + p[i].x;
	}
	return sum;  // Should be 60
}
