#ifndef CPYCPPYY_LOWLEVELVIEWS_H
#define CPYCPPYY_LOWLEVELVIEWS_H

namespace CPyCppyy {

PyObject* LowLevel_MemoryView(bool*,             Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(short*,            Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(unsigned short*,   Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(Int_t*,            Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(UInt_t*,           Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(Long_t*,           Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(ULong_t*,          Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(float*,            Py_ssize_t size = -1);
PyObject* LowLevel_MemoryView(double*,           Py_ssize_t size = -1);

} // namespace CPyCppyy

#endif // !CPYCPPYY_LOWLEVELVIEWS_H
