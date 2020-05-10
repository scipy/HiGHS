
from libcpp.memory cimport unique_ptr

from HMatrix cimport HMatrix

cdef class linalg:

    cdef unique_ptr[HMatrix] _owner
    cdef HMatrix * _ptr
