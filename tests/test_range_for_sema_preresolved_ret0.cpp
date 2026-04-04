// Test: range-for with struct iterator - dereference function should be
// pre-resolved by sema. This exercises Phase 4 of the codegen lookup cleanup
// where codegen no longer calls back into sema for iterator dereference.

struct IntIter {
int* ptr;

int& operator*();
IntIter& operator++();
bool operator!=(IntIter other);
};

int& IntIter::operator*() {
return *ptr;
}

IntIter& IntIter::operator++() {
++ptr;
return *this;
}

bool IntIter::operator!=(IntIter other) {
return ptr != other.ptr;
}

struct Numbers {
int data[5];

IntIter begin();
IntIter end();
};

IntIter Numbers::begin() {
IntIter it{&data[0]};
return it;
}

IntIter Numbers::end() {
IntIter it{&data[5]};
return it;
}

int main() {
Numbers n{{5, 10, 15, 20, 50}};
int sum = 0;
for (auto value : n) {
sum += value;
}
// 5 + 10 + 15 + 20 + 50 = 100
if (sum != 100)
return 1;

// Test with reference
int product = 1;
Numbers m{{1, 2, 3, 4, 5}};
for (auto& val : m) {
product *= val;
}
// 1 * 2 * 3 * 4 * 5 = 120
if (product != 120)
return 2;

return 0;
}
