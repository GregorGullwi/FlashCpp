struct Vault {
private:
	static int secret;
};

int Vault::secret = 42;

int read() {
	return Vault::secret;
}

int main() {
	return read();
}
