# distutils: language=c++
# cython: language_level=3

from libcpp cimport bool

from HighsOptions cimport HighsOptions
from HighsLp cimport HighsLp
from PresolveComponent cimport PresolveComponent

cdef extern from "HighsMipSolver.h" nogil:
    # From HiGHS/src/mip/HighsMipSolver.h
    cdef cppclass HighsMipSolver:
        HighsMipSolver(const HighsOptions& options, const HighsLp& lp, bool submip = False) except +
        void run()

        const HighsLp* model_
        const HighsOptions* options_mip_
        PresolveComponent presolve_
