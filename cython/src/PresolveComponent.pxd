# distutils: language=c++
# cython: language_level=3

from HighsLp cimport HighsSolution

cdef extern from "PresolveComponent.h" nogil:
    # From HiGHS/src/presolve/PresolveComponent.h
    cdef cppclass PresolveComponent:
        PresolveComponentData data_

    cdef cppclass PresolveComponentData:
        HighsSolution recovered_solution_
