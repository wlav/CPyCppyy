#!/usr/bin/env python

import os, glob
from setuptools import setup, find_packages, Extension
from codecs import open


here = os.path.abspath(os.path.dirname(__file__))
with open(os.path.join(here, 'README.rst'), encoding='utf-8') as f:
    long_description = f.read()


setup(
    name='CPyCppyy',
    version='0.1.2',
    description='Cling-based Python-C++ bindings',
    long_description=long_description,

    url='http://doc.pypy.org/en/latest/cppyy.html',

    # Author details
    author='Wim Lavrijsen',
    author_email='WLavrijsen@lbl.gov',

    license='LBNL BSD',

    classifiers=[
        'Development Status :: 4 - Beta',

        'Intended Audience :: Developers',

        'Topic :: Software Development',
        'Topic :: Software Development :: Interpreters',

        #'License :: OSI Approved :: MIT License',

        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: C',
        'Programming Language :: C++',

        'Natural Language :: English'
    ],

    install_requires=['PyPy-cppyy-backend'],

    keywords='C++ bindings',

    package_dir={'': 'python'},
    packages=find_packages('python'),

    ext_modules=[Extension('libcppyy',
        sources=glob.glob('src/*.cxx'),
        extra_compile_args=['-std=c++11', '-O2'],
        include_dirs=['include'])],

)

