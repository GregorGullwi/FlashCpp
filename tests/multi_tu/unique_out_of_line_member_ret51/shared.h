// Unique out-of-line members have strong linkage. Defining them in only one TU
// must still link; emitting them as SELECT_ANY would hide a later LNK2005 when
// two TUs both provide the same out-of-line body.

struct UniqueOutOfLine {
	UniqueOutOfLine();
	~UniqueOutOfLine();
	int uniqueValue() const;
	short extra() const;

private:
	int value_;
	short extra_;
};

int uniqueFreeHelper();
