""" Dynamic C++ bindings generator.
"""

import sys
from . import _pythonization


### helpers
def _load_backend():
    import ctypes
    try:
      # attempt to locate libcppyy_backend.so the normal way
        c = ctypes.CDLL("libcppyy_backend.so", ctypes.RTLD_GLOBAL)
    except OSError:
      # try to locate it in the expected location
        import os
        for path in sys.path:
            if os.path.exists(os.path.join(path, 'cppyy_backend/lib/libcppyy_backend.so')):
              # preload dependencies
                libpath = os.path.join(path, 'cppyy_backend/lib')
                for dep in ['libCore.so', 'libThread.so', 'libRIO.so', 'libCling.so']:
                    ctypes.CDLL(os.path.join(libpath, dep), ctypes.RTLD_GLOBAL)
                c = ctypes.CDLL(os.path.join(libpath, 'libcppyy_backend.so'), ctypes.RTLD_GLOBAL)
                break
        else:
            raise
    return c


### PyPy has 'cppyy' builtin (if enabled, that is)
if 'cppyy' in sys.builtin_module_names:
    _builtin_cppyy = True

    c = _load_backend()

    import imp
    sys.modules[ __name__ ] = \
        imp.load_module( 'cppyy', *(None, 'cppyy', ('', '', imp.C_BUILTIN) ) )
    del imp

    _thismodule = sys.modules[ __name__ ]
    _backend = _thismodule.gbl
    _backend.cpp_backend = c
    _thismodule._backend = _backend

  # custom behavior that is not yet part of PyPy's cppyy
    def _CreateScopeProxy(self, name):
        return getattr(self, name)
    type(_backend).CreateScopeProxy = _CreateScopeProxy

    def _LookupCppEntity(self, name):
        return getattr(self, name)
    type(_backend).LookupCppEntity = _LookupCppEntity

    del _LookupCppEntity, _CreateScopeProxy

else:
    _builtin_cppyy = False

    import ctypes
    try:
      # attempt to locate libcppyy_backend.so the normal way
        c = ctypes.CDLL("libcppyy_backend.so", ctypes.RTLD_GLOBAL)
    except OSError:
      # try to locate it in the expected location
        c = _load_backend()

    import libcppyy as _backend
    _backend.cpp_backend = c

    _thismodule = sys.modules[__name__]

    _backend.SetMemoryPolicy(_backend.kMemoryStrict)


### -----------------------------------------------------------------------------
### -- metaclass helper from six ------------------------------------------------
### -- https://bitbucket.org/gutworth/six/src/8a545f4e906f6f479a6eb8837f31d03731597687/six.py?at=default#cl-800
#
# Copyright (c) 2010-2015 Benjamin Peterson
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

def with_metaclass(meta, *bases):
    """Create a base class with a metaclass."""
    # This requires a bit of explanation: the basic idea is to make a dummy
    # metaclass for one level of class instantiation that replaces itself with
    # the actual metaclass.
    class metaclass(meta):
        def __new__(cls, name, this_bases, d):
            return meta(name, bases, d)
    return type.__new__(metaclass, 'temporary_class', (), {})

### -----------------------------------------------------------------------------

### template support ------------------------------------------------------------
if not _builtin_cppyy:
  # TODO: why is this necessary??
    class Template:
        def __init__(self, name):
            self.__name__ = name

        def __repr__(self):
            return "<cppyy.Template '%s' object at %s>" % (self.__name__, hex(id(self)))

        def __call__(self, *args):
            newargs = [self.__name__]
            for arg in args:
                if type(arg) == str:
                    arg = ','.join(map(lambda x: x.strip(), arg.split(',')))
                newargs.append(arg)
            result = _backend.MakeCppTemplateClass( *newargs )

          # special case pythonization (builtin_map is not available from the C-API)
            if 'push_back' in result.__dict__:
                def iadd(self, ll):
                    [self.push_back(x) for x in ll]
                    return self
                result.__iadd__ = iadd

            return result

    _backend.Template = Template


#--- Other functions needed -------------------------------------------
if not _builtin_cppyy:
    class _ns_meta(type):
        def __getattr__(cls, name):
            try:
                attr = _backend.LookupCppEntity(name)
            except TypeError as e:
                raise AttributeError(str(e))
            if type(attr) is _backend.PropertyProxy:
                setattr(cls.__class__, name, attr)
                return attr.__get__(cls)
            setattr(cls, name, attr)
            return attr

    class _stdmeta(type):
        def __getattr__(cls, name):     # for non-templated classes in std
            try:
              # TODO: why only classes here?
                klass = _backend.CreateScopeProxy(name, cls)
            except TypeError as e:
                raise AttributeError(str(e))
            setattr( cls, name, klass )
            return klass

    class _global_cpp(with_metaclass(_ns_meta)):
        class std(with_metaclass(_stdmeta, object)):
          # pre-get string to ensure disambiguation from python string module
            string = _backend.CreateScopeProxy('string')

    gbl = _global_cpp
    sys.modules['cppyy.gbl'] = gbl

  # cppyy-style functions
    addressof = _backend.addressof
    bind_object = _backend.bind_object

    def load_reflection_info(name):
        sc = gbl.gSystem.Load(name)
        if sc == -1:
            raise RuntimeError("missing reflection library "+name)

else:
    _global_cpp = _backend

  # copy over locally defined names
    for name in dir():
        if name[0] != '_': setattr(_thismodule, name, eval(name))
 
def add_smart_pointer(typename):
    """Add a smart pointer to the list of known smart pointer types.
    """
    _backend.AddSmartPtrType(typename)


#--- Pythonization factories --------------------------------------------
_pythonization._set_backend(_backend)
from _pythonization import *
del _pythonization


#--- CFFI style interface -----------------------------------------------
def cppdef( src ):
    _thismodule.gbl.gInterpreter.Declare(src)
_thismodule.cppdef = cppdef


#--- Enable auto-loading; ignoring possible error for the time being ----
try:    gbl.gInterpreter.EnableAutoLoading()
except: pass
