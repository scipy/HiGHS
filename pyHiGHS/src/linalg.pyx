# distutils: language=c++
# cython: language_level=3

# TODO:
#    There seem to be no less than 3 different linear solvers:
#        simplex (HFactor.h) -- mainly focused around simplex operations
#        basiclu (basiclu_factorize/_solve_dense/_solve_sparse/_solve_for_update.h) -- a little more general
#        ipx (lu_factorization/linear_operator/lu_update/) -- might be build on basiclu, can't tell until I dig in

from scipy.sparse import csc_matrix

from libcpp.memory cimport unique_ptr

from HMatrix cimport HMatrix
from linalg cimport linalg

cdef class linalg:

    def __cinit__(self, int numCol, int numRow, A, const int[::1] nonbasicFlag):

        assert isinstance(A, csc_matrix), 'A must be a csc_matrix!'

        self._owner = unique_ptr[HMatrix](new HMatrix())
        self._ptr = self._owner.get()

        cdef int[::1] Aindptr = A.indptr
        cdef int[::1] Aindices = A.indices
        cdef double[::1] Adata = A.data
        cdef int * Astart = &Aindptr[0]
        cdef int * Aindex = &Aindices[0]
        cdef double * Avalues = &Adata[0]
        cdef const int * nbF = &nonbasicFlag[0]

        self._ptr.setup(
            numCol,
            numRow,
            Astart,
            Aindex,
            Avalues,
            nbF,
        )
