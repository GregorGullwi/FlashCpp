struct E {};

struct Bad : E, E {};

int main() {
	return 0;
}
