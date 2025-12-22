int main() {
    int eq = (10 == 10);        // 1 (true)
    int ne = (10 != 5);         // 1 (true)
    int lt = (5 < 10);          // 1 (true)
    int le = (10 <= 10);        // 1 (true)
    int gt = (15 > 10);         // 1 (true)
    int ge = (10 >= 10);        // 1 (true)
    
    unsigned int ult = (5u < 10u);      // 1 (unsigned less than)
    unsigned int uge = (10u >= 5u);     // 1 (unsigned greater equal)
    
    // Total: 1+1+1+1+1+1+1+1 = 8
    return eq + ne + lt + le + gt + ge + ult + uge;
}
