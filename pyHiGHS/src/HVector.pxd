
from libcpp cimport bool
from libcpp.vector cimport vector

cdef extern from "HVector.h" namespace "" nogil:
    cdef cppclass HVector:
        void setup(int size_)
        void clear()

        int size
        int count
        vector[int] index
        vector[double] array

        double syntheticTick

        void tight()
        void pack()
        bool packFlag
        int packCount
        vector[int] packIndex
        vector[double] packValue

        void copy(const HVector * from_)

        double norm2()

        void saxpy(const double pivotX, const HVector * pivot)
