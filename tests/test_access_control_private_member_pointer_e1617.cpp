struct Vault {
private:
	int secret = 42;
};

int read(const Vault& vault) {
	auto member = &Vault::secret;
	return vault.*member;
}

int main() {
	Vault vault;
	return read(vault);
}
