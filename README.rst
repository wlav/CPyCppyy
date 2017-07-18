.. -*- mode: rst -*-

CPyCppyy: Python-C++ bindings interface based on Cling/LLVM
===========================================================

CPyCppyy is the CPython equivalent of cppyy in PyPy.
It provides dynamic Python-C++ bindings by leveraging the Cling C++
interpreter and LLVM.
Details and performance are described in
`this paper <http://conferences.computer.org/pyhpc/2016/papers/5220a027.pdf>`_.

CPyCppyy is a CPython extension module built on top of the same backend API
as PyPy/cppyy.
It thus requires the installation of the
`PyPy cppyy backend <https://pypi.python.org/pypi/PyPy-cppyy-backend/>`_
for use.
CPython/cppyy and PyPy/cppyy are designed to be compatible, although there
are differences due to the former being reference counted and the latter
being garbage collected.

Read the `full documentation <http://cppyy.readthedocs.io/en/latest/>`.
