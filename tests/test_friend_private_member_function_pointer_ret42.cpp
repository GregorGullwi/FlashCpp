class Vault {
	friend int read(const Vault& vault);

private:
	int secret() const { return 42; }
};

int read(const Vault& vault) {
	auto member = &Vault::secret;
	(void)vault;
	(void)member;
	return 42;
}

int main() {
	Vault vault;
	return read(vault);
}
