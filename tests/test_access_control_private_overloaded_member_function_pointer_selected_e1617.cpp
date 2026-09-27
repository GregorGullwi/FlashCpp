class Vault {
public:
	int read(int value) const { return value; }

private:
	int read(double value) const { return static_cast<int>(value); }
};

int main() {
	auto member = static_cast<int (Vault::*)(double) const>(&Vault::read);
	(void)member;
	return 0;
}
