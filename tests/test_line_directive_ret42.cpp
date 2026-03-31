// Test that #line directive properly updates line number and filename.
// __LINE__ on the line immediately after #line N is N, so we use N=42.
int main() {
#line 42 "fake_file.cpp"
	return __LINE__;	 // __LINE__ should be 42
}
