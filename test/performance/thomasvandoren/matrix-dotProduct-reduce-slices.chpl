/* Chapelerific matrix dot product.
*/

use TestHelpers;

// Calculate C = A * B, then print C.
proc main() {
  timer.start();

  dotProduct(C, A, B);

  timer.stop();
  printResults();
}

proc dotProduct(ref C: [?DC] int, ref A: [?DA] int, ref B: [?DB] int)
  where DC.rank == 2 && DA.rank == 2 && DB.rank == 2
{
  checkDims(DC, DA, DB);

  forall (row, col) in C.domain do
    C[row, col] = + reduce (A[row, 1..] * B[1.., col]);
}
