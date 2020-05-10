
from HVector cimport HVector

cdef extern from "HMatrix.h" namespace "" nogil:
    cdef cppclass HMatrix:
        HMatrix() except +

        void setup(
            int numCol,
            int numRow,
            const int * Astart,
            const int * Aindex,
            const double * Avalue,
            const int * nonbasicFlag,
        )

        void setup_lgBs(
            int numCol,
            int numRow,
            const int * Astart,
            const int * Aindex,
            const double * Avalue,
        )

        # y.T = x.T @ A
        void priceByColumn(HVector & row_ap, const HVector & row_ep) const

        # y.T = x.T @ N
        void priceByRowSparseResult(HVector & row_ap, const HVector & row_ep) const

        void update(int columnIn, int columnOut)

        # x.T @ Ai
        double compute_dot(HVector & vector, int iCol) const

        # x = x + mu*Ai
        void collect_aj(HVector & vector, int iCol, double multiplier) const

        const int * getAstart() const

        const int * getAindex() const

        const double * getAvalue() const

        const double hyperPrice
