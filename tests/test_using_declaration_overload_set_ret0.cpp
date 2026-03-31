namespace First {
int select(double) {
	return 1;
}
} // namespace First

namespace Second {
int select(int) {
	return 0;
}
} // namespace Second

int main() {
	using First::select;
	using Second::select;
	return select(0);
}
