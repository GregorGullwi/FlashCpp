struct AllSizes { char c = 5; short s = 10; int i = 20; long long ll = 40; }; int main() { AllSizes a; AllSizes b = a; return b.i; }
