from distutils.core import setup
from distutils.extension import Extension
from Cython.Build import cythonize

import numpy as np

extensions = [
    Extension(
        'pyHiGHS.libhighs',
        [
            'src/io/Filereader.cpp',
            'src/io/FilereaderLp.cpp',
            'src/io/FilereaderEms.cpp',
            'src/io/FilereaderMps.cpp',
            'src/io/HighsIO.cpp',
            'src/io/HMPSIO.cpp',
            'src/io/HMpsFF.cpp',
            'src/io/HToyIO.cpp',
            'src/io/LoadOptions.cpp',
            'src/io/LoadProblem.cpp',
            'src/lp_data/Highs.cpp',
            'src/lp_data/HighsInfo.cpp',
            'src/lp_data/HighsLp.cpp',
            'src/lp_data/HighsLpUtils.cpp',
            'src/lp_data/HighsModelUtils.cpp',
            'src/lp_data/HighsModelBuilder.cpp',
            'src/lp_data/HighsSolution.cpp',
            'src/lp_data/HighsSolve.cpp',
            'src/lp_data/HighsStatus.cpp',
            'src/lp_data/HighsOptions.cpp',
            'src/mip/HighsMipSolver.cpp',
            'src/mip/SolveMip.cpp',
            'src/presolve/Presolve.cpp',
            'src/presolve/PresolveAnalysis.cpp',
            'src/presolve/HPreData.cpp',
            'src/simplex/HCrash.cpp',
            'src/simplex/HDual.cpp',
            'src/simplex/HDualRHS.cpp',
            'src/simplex/HDualRow.cpp',
            'src/simplex/HDualMulti.cpp',
            'src/simplex/HFactor.cpp',
            'src/simplex/HighsSimplexAnalysis.cpp',
            'src/simplex/HighsSimplexInterface.cpp',
            'src/simplex/HMatrix.cpp',
            'src/simplex/HPrimal.cpp',
            'src/simplex/HQPrimal.cpp',
            'src/simplex/HSimplex.cpp',
            'src/simplex/HVector.cpp',
            'src/test/KktCheck.cpp',
            'src/test/KktChStep.cpp',
            'src/util/HighsSort.cpp',
            'src/util/HighsUtils.cpp',
            'src/util/stringutil.cpp',
            'src/interfaces/highs_c_api.cpp',
        ],
        include_dirs=[
            'src/',
            'src/io/',
            'src/lp_data/',
            'src/util/',
        ],
    ),
    Extension(
        'pyHiGHS.mpswriter',
        ['pyHiGHS/src/mpswriter.pyx'],
        include_dirs=[
            'pyHiGHS/src/',
            'src/',
            'src/lp_data/',
            'src/io/',
            'src/util/',
            np.get_include(),
        ],
        library_dirs=['pyHiGHS'],
        libraries=['highs.cpython-36m-x86_64-linux-gnu'],
        runtime_library_dirs=['pyHiGHS'],
    ),
]

setup(
    ext_modules=cythonize(extensions),
)
