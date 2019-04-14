#include "Highs.h"

extern "C" void callhighs(
   int numcol, 
   int numrow, 
   double* colcost, 
   double* collower,
   double* colupper, 
   double* rowlower, 
   double* rowupper,
   int* astart, 
   int* aindex, 
   double* avalue
   ) {
  int local_numNz = astart[numcol];
     HighsLp lp;
     lp.numCol_ = numcol;
     lp.numRow_ = numrow;
     lp.numInt_ = 0;
     
     lp.colCost_.resize(numcol);
     lp.colLower_.resize(numcol);
     lp.colUpper_.resize(numcol);

     lp.rowLower_.resize(numrow);
     lp.rowUpper_.resize(numrow);
     lp.Astart_.resize(numcol+1);
     lp.Aindex_.resize(local_numNz);
     lp.Avalue_.resize(local_numNz);

     lp.colCost_.assign(colcost, colcost + numcol);
     lp.colLower_.assign(collower, collower + numcol);
     lp.colUpper_.assign(colupper, colupper + numcol);

     lp.rowLower_.assign(rowlower, rowlower + numrow);
     lp.rowUpper_.assign(rowupper, rowupper + numcol);
     lp.Astart_.assign(astart, astart + numcol + 1);
     lp.Aindex_.assign(aindex, aindex + local_numNz);
     lp.Avalue_.assign(avalue, avalue + local_numNz);

     HighsOptions options;

     Highs highs(options);

     highs.initializeLp(lp);
     highs.run();

   }
