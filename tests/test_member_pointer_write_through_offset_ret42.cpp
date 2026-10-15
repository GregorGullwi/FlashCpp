// A member pointer to a non-leading member must apply the member offset when
// storing through `.*`.

struct Pair {
	int first;
	int second;
};

int main() {
	Pair value{0, 0};
	int Pair::* member = &Pair::second;
	value.*member = 42;
	return value.second;
}
