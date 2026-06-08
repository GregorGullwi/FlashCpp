struct Value {
	bool operator!(int extra) const {
		return extra != 0;
	}
};

int main() {
	return 0;
}
