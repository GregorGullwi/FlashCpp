class Vault {
private:
	int secret() const { return 1; }
};

int main() {
	auto member = &Vault::secret;
	(void)member;
	return 0;
}
