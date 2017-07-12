Installation
============

This assumes PyPy2.7 v5.7 or later; earlier versions use a Reflex-based cppyy
module, which is no longer supported.
Both the tooling and user-facing Python codes are very backwards compatible,
however.
Further dependencies are cmake (for general build), Python2.7 (for LLVM), and
a modern C++ compiler (one that supports at least C++11).

Assuming you have a recent enough version of PyPy installed, use pip to
complete the installation of cppyy::

 $ MAKE_NPROCS=4 pypy-c -m pip install --verbose PyPy-cppyy-backend

Set the number of parallel builds ('4' in this example, through the MAKE_NPROCS
environment variable) to a number appropriate for your machine.
The building process may take quite some time as it includes a customized
version of LLVM as part of Cling, which is why --verbose is recommended so that
you can see the build progress.

The default installation will be under
$PYTHONHOME/site-packages/cppyy_backend/lib,
which needs to be added to your dynamic loader path (LD_LIBRARY_PATH).
If you need the dictionary and class map generation tools (used in the examples
below), you need to add $PYTHONHOME/site-packages/cppyy_backend/bin to your
executable path (PATH).
