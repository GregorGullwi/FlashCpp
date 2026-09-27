struct Vault {
protected:
	int secret = 42;
	friend int read(const Vault& vault);
};

int read(const Vault& vault) {
	return vault.secret;
}

int main() {
	Vault vault;
	return read(vault);
}
