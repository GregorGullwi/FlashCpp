class Vault {
private:
	int secret(int value) const { return value; }
	int secret(double value) const { return static_cast<int>(value); }
};

int main() {
	auto member = static_cast<int (Vault::*)(int) const>(&Vault::secret);
	(void)member;
	return 0;
}
