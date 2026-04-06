__thread int g_value = 7;

int main() {
	return g_value == 7 ? 0 : 1;
}
