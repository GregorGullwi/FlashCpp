struct Vault {
private:
	int secret = 42;
	friend int read(const Vault& vault);
};

int read(const Vault& vault) {
	auto member = &Vault::secret;
	return vault.*member;
}

int main() {
	Vault vault;
	return read(vault);
}
