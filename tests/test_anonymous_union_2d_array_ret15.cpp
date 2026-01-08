// Test 2D array in anonymous union
struct Matrix {
   int mode;
   union {
       int value;
       int matrix[3][3];
   };
};

int main() {
   Matrix m;
   m.mode = 2;
   
   // Initialize a 3x3 matrix
   m.matrix[0][0] = 1;
   m.matrix[0][1] = 2;
   m.matrix[0][2] = 3;
   m.matrix[1][0] = 4;
   m.matrix[1][1] = 5;
   m.matrix[1][2] = 0;
   
   // Sum some elements: 1+2+3+4+5 = 15
   int sum = m.matrix[0][0] + m.matrix[0][1] + m.matrix[0][2] + 
             m.matrix[1][0] + m.matrix[1][1];
   
   return sum;
}
