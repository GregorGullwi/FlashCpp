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
	using namespace First;
	using namespace Second;
	return select(0);
}
