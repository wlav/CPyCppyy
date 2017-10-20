// Bindings
#include "CPyCppyy.h"
#include "PyStrings.h"
#include "DeclareExecutors.h"
#include "ObjectProxy.h"
#include "TPyBufferFactory.h"
#include "TypeManip.h"
#include "CPyCppyyHelpers.h"
#include "Utility.h"

// Standard
#include <cstring>
#include <map>
#include <sstream>
#include <utility>


//- data ______________________________________________________________________
namespace CPyCppyy {

    typedef TExecutor* (*ExecutorFactory_t) ();
    typedef std::map<std::string, ExecutorFactory_t> ExecFactories_t;
    static ExecFactories_t gExecFactories;

    extern PyObject* gNullPtrObject;
}


//- helpers -------------------------------------------------------------------
namespace {

    class GILControl {
    public:
        GILControl(CPyCppyy::TCallContext* ctxt) :
                fSave(nullptr ), fRelease(ReleasesGIL(ctxt)) {
#ifdef WITH_THREAD
            if (fRelease) fSave = PyEval_SaveThread();
#endif
        }
        ~GILControl() {
#ifdef WITH_THREAD
            if (fRelease) PyEval_RestoreThread(fSave);
#endif
        }
    private:
        PyThreadState* fSave;
        bool fRelease;
    };

} // unnamed namespace

#define CPPYY_IMPL_GILCALL(rtype, tcode)                                \
    static inline rtype GILCall##tcode(                                 \
        Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, CPyCppyy::TCallContext* ctxt) { \
        GILControl gc(ctxt);                                            \
        return Cppyy::Call##tcode(method, self, &ctxt->fArgs);          \
}

CPPYY_IMPL_GILCALL(void,          V)
CPPYY_IMPL_GILCALL(unsigned char, B)
CPPYY_IMPL_GILCALL(char,          C)
CPPYY_IMPL_GILCALL(short,         H)
CPPYY_IMPL_GILCALL(Int_t,         I)
CPPYY_IMPL_GILCALL(Long_t,        L)
CPPYY_IMPL_GILCALL(Long64_t,      LL)
CPPYY_IMPL_GILCALL(float,         F)
CPPYY_IMPL_GILCALL(double,        D)
CPPYY_IMPL_GILCALL(LongDouble_t,  LD)
CPPYY_IMPL_GILCALL(void*,         R)

// TODO: CallS may not have a use here; CallO is used instead for std::string
static inline char* GILCallS(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, CPyCppyy::TCallContext* ctxt)
{
    GILControl gc(ctxt);
// TODO: make use of getting the string length returned ...
    size_t len;
    return Cppyy::CallS(method, self, &ctxt->fArgs, &len);
}

static inline Cppyy::TCppObject_t GILCallO(Cppyy::TCppMethod_t method,
    Cppyy::TCppObject_t self, CPyCppyy::TCallContext* ctxt, Cppyy::TCppType_t klass)
{
    GILControl gc(ctxt);
    return Cppyy::CallO(method, self, &ctxt->fArgs, klass);
}

static inline Cppyy::TCppObject_t GILCallConstructor(
    Cppyy::TCppMethod_t method, Cppyy::TCppType_t klass, CPyCppyy::TCallContext* ctxt)
{
    GILControl gc(ctxt);
    return Cppyy::CallConstructor(method, klass, &ctxt->fArgs);
}

static inline PyObject* CPyCppyy_PyUnicode_FromInt(int c)
{
// python chars are range(256)
    if (c < 0) return CPyCppyy_PyUnicode_FromFormat("%c", 256 - std::abs(c));
    return CPyCppyy_PyUnicode_FromFormat("%c", c);
}

static inline PyObject* CPyCppyy_PyBool_FromInt(int b)
{
    PyObject* result = (bool)b ? Py_True : Py_False;
    Py_INCREF(result);
    return result;
}


//- executors for built-ins ---------------------------------------------------
PyObject* CPyCppyy::TBoolExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python bool return value
    bool retval = GILCallB(method, self, ctxt);
    PyObject* result = retval ? Py_True : Py_False;
    Py_INCREF(result);
    return result;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TBoolConstRefExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python bool return value
    return CPyCppyy_PyBool_FromInt(*((bool*)GILCallR(method, self, ctxt)));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCharExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method with argument <self, ctxt>, construct python string return value
// with the single char
    return CPyCppyy_PyUnicode_FromInt((int)GILCallC(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCharConstRefExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python string return value
// with the single char
    return CPyCppyy_PyUnicode_FromInt(*((char*)GILCallR(method, self, ctxt)));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TUCharExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, args>, construct python string return value
// with the single char
    return CPyCppyy_PyUnicode_FromInt((unsigned char)GILCallB(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TUCharConstRefExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python string return value
// with the single char from the pointer return
    return CPyCppyy_PyUnicode_FromInt(*((unsigned char*)GILCallR(method, self, ctxt)));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TIntExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python int return value
    return PyInt_FromLong((int)GILCallI(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TShortExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python int return value
    return PyInt_FromLong((short)GILCallH(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TLongExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python long return value
    return PyLong_FromLong((Long_t)GILCallL(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TULongExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python unsigned long return value
   return PyLong_FromUnsignedLong((ULong_t)GILCallLL(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TLongLongExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt )
{
// execute <method> with argument <self, ctxt>, construct python long long return value
    Long64_t result = GILCallLL( method, self, ctxt );
    return PyLong_FromLongLong( result );
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TULongLongExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python unsigned long long return value
    ULong64_t result = (ULong64_t)GILCallLL(method, self, ctxt);
    return PyLong_FromUnsignedLongLong(result);
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TFloatExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python float return value
    return PyFloat_FromDouble((double)GILCallF(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TDoubleExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python float return value
    return PyFloat_FromDouble((double)GILCallD(method, self, ctxt));
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TLongDoubleExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python float return value
    return PyFloat_FromDouble((double)GILCallLD(method, self, ctxt));
}

//-----------------------------------------------------------------------------
bool CPyCppyy::TRefExecutor::SetAssignable(PyObject* pyobject)
{
// prepare "buffer" for by-ref returns, used with __setitem__
    if (pyobject) {
        Py_INCREF(pyobject);
        fAssignable = pyobject;
        return true;
    }

    fAssignable = nullptr;
    return false;
}

//-----------------------------------------------------------------------------
#define CPPYY_IMPL_REFEXEC(name, type, stype, F1, F2)                         \
PyObject* CPyCppyy::T##name##RefExecutor::Execute(                            \
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt) \
{                                                                             \
    type* ref = (type*)GILCallR(method, self, ctxt);                          \
    if (!fAssignable)                                                         \
        return F1((stype)*ref);                                               \
    else {                                                                    \
        *ref = (type)F2(fAssignable);                                         \
        Py_DECREF(fAssignable);                                               \
        fAssignable = nullptr;                                                \
        Py_INCREF(Py_None);                                                   \
        return Py_None;                                                       \
    }                                                                         \
}

CPPYY_IMPL_REFEXEC(Bool,   bool,   Long_t,   CPyCppyy_PyBool_FromInt,    PyLong_AsLong)
CPPYY_IMPL_REFEXEC(Char,   char,   Long_t,   CPyCppyy_PyUnicode_FromInt, PyLong_AsLong)
CPPYY_IMPL_REFEXEC(UChar,  unsigned char,  ULong_t,  CPyCppyy_PyUnicode_FromInt, PyLongOrInt_AsULong)
CPPYY_IMPL_REFEXEC(Short,  short,  Long_t,   PyInt_FromLong,     PyLong_AsLong)
CPPYY_IMPL_REFEXEC(UShort, unsigned short, ULong_t,  PyInt_FromLong,     PyLongOrInt_AsULong)
CPPYY_IMPL_REFEXEC(Int,    Int_t,    Long_t,   PyInt_FromLong,     PyLong_AsLong)
CPPYY_IMPL_REFEXEC(UInt,   UInt_t,   ULong_t,  PyLong_FromUnsignedLong, PyLongOrInt_AsULong)
CPPYY_IMPL_REFEXEC(Long,   Long_t,   Long_t,   PyLong_FromLong,    PyLong_AsLong)
CPPYY_IMPL_REFEXEC(ULong,  ULong_t,  ULong_t,  PyLong_FromUnsignedLong, PyLongOrInt_AsULong)
CPPYY_IMPL_REFEXEC(LongLong,  Long64_t,  Long64_t,   PyLong_FromLongLong,         PyLong_AsLongLong)
CPPYY_IMPL_REFEXEC(ULongLong, ULong64_t, ULong64_t,  PyLong_FromUnsignedLongLong, PyLongOrInt_AsULong64)
CPPYY_IMPL_REFEXEC(Float,  float,  double, PyFloat_FromDouble, PyFloat_AsDouble)
CPPYY_IMPL_REFEXEC(Double, double, double, PyFloat_FromDouble, PyFloat_AsDouble)
CPPYY_IMPL_REFEXEC(LongDouble, LongDouble_t, LongDouble_t, PyFloat_FromDouble, PyFloat_AsDouble)

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TSTLStringRefExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, return python string return value
    if (!fAssignable) {
        std::string* result = (std::string*)GILCallR(method, self, ctxt);
        return CPyCppyy_PyUnicode_FromStringAndSize(result->c_str(), result->size());
    } else {
        std::string* result = (std::string*)GILCallR(method, self, ctxt);
        *result = std::string(
            CPyCppyy_PyUnicode_AsString(fAssignable), CPyCppyy_PyUnicode_GET_SIZE(fAssignable));

        Py_DECREF(fAssignable);
        fAssignable = nullptr;

        Py_RETURN_NONE;
    }
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TVoidExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, return None
    GILCallV(method, self, ctxt);
    Py_RETURN_NONE;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCStringExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python string return value
    char* result = (char*)GILCallR(method, self, ctxt);
    if (!result) {
        Py_INCREF(PyStrings::gEmptyString);
        return PyStrings::gEmptyString;
    }

    return CPyCppyy_PyUnicode_FromString(result);
}


//- pointer/array executors ---------------------------------------------------
PyObject* CPyCppyy::TVoidArrayExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python long return value
    Long_t* result = (Long_t*)GILCallR(method, self, ctxt);
    if (!result) {
        Py_INCREF(gNullPtrObject);
        return gNullPtrObject;
    }
    return BufFac_t::Instance()->PyBuffer_FromMemory(result, sizeof(void*));
}

//-----------------------------------------------------------------------------
#define CPPYY_IMPL_ARRAY_EXEC(name, type)                                     \
PyObject* CPyCppyy::T##name##ArrayExecutor::Execute(                          \
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt) \
{                                                                             \
    return BufFac_t::Instance()->PyBuffer_FromMemory((type*)GILCallR(method, self, ctxt));\
}

CPPYY_IMPL_ARRAY_EXEC(Bool,   bool)
CPPYY_IMPL_ARRAY_EXEC(Short,  short)
CPPYY_IMPL_ARRAY_EXEC(UShort, unsigned short)
CPPYY_IMPL_ARRAY_EXEC(Int,    Int_t)
CPPYY_IMPL_ARRAY_EXEC(UInt,   UInt_t)
CPPYY_IMPL_ARRAY_EXEC(Long,   Long_t)
CPPYY_IMPL_ARRAY_EXEC(ULong,  ULong_t)
CPPYY_IMPL_ARRAY_EXEC(Float,  float)
CPPYY_IMPL_ARRAY_EXEC(Double, double)


//- special cases ------------------------------------------------------------
PyObject* CPyCppyy::TSTLStringExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python string return value

// TODO: make use of GILLCallS (?!)
    static Cppyy::TCppScope_t sSTLStringScope = Cppyy::GetScope("std::string");
    std::string* result = (std::string*)GILCallO(method, self, ctxt, sSTLStringScope);
    if (!result) {
        Py_INCREF(PyStrings::gEmptyString);
        return PyStrings::gEmptyString;
    }

    PyObject* pyresult =
        CPyCppyy_PyUnicode_FromStringAndSize(result->c_str(), result->size());
    free(result); // GILCallO calls Cppyy::CallO which calls malloc.

    return pyresult;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python proxy object return value
    return BindCppObject((void*)GILCallR(method, self, ctxt), fClass);
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectByValueExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execution will bring a temporary in existence
    Cppyy::TCppObject_t value = GILCallO(method, self, ctxt, fClass);

    if (!value) {
        if (!PyErr_Occurred())         // callee may have set a python error itself
            PyErr_SetString(PyExc_ValueError, "nullptr result where temporary expected");
        return nullptr;
    }

// the result can then be bound
    ObjectProxy* pyobj = (ObjectProxy*)BindCppObjectNoCast(value, fClass, false, true);
    if (!pyobj)
        return nullptr;

// python ref counting will now control this object's life span
    pyobj->PythonOwns();
    return (PyObject*)pyobj;
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectRefExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// executor binds the result to the left-hand side, overwriting if an old object
    PyObject* result = BindCppObject((void*)GILCallR(method, self, ctxt), fClass);
    if (!result || !fAssignable)
        return result;
    else {
    // this generic code is quite slow compared to its C++ equivalent ...
        PyObject* assign = PyObject_GetAttr(result, PyStrings::gAssign);
        if (!assign) {
            PyErr_Clear();
            PyObject* descr = PyObject_Str(result);
            if (descr && CPyCppyy_PyUnicode_CheckExact(descr)) {
                PyErr_Format(PyExc_TypeError, "can not assign to return object (%s)",
                             CPyCppyy_PyUnicode_AsString(descr));
            } else {
                PyErr_SetString(PyExc_TypeError, "can not assign to result");
            }
            Py_XDECREF(descr);
            Py_DECREF(result);
            Py_DECREF(fAssignable); fAssignable = nullptr;
            return nullptr;
        }

        PyObject* res2 = PyObject_CallFunction(assign, const_cast<char*>("O"), fAssignable);

        Py_DECREF(assign);
        Py_DECREF(result);
        Py_DECREF(fAssignable); fAssignable = nullptr;

        if (res2) {
            Py_DECREF(res2);            // typically, *this from operator=()
            Py_RETURN_NONE;
        }

        return nullptr;
    }
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectPtrPtrExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python C++ proxy object
// return ptr value
    return BindCppObject((void*)GILCallR(method, self, ctxt), fClass, true);
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectPtrRefExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct python C++ proxy object
// ignoring ref) return ptr value
    return BindCppObject(*(void**)GILCallR(method, self, ctxt), fClass, false);
}


//- smart pointers ------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectBySmartPtrExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// smart pointer executor
    Cppyy::TCppObject_t value = GILCallO( method, self, ctxt, fClass);

    if (!value) {
        if (!PyErr_Occurred())          // callee may have set a python error itself
            PyErr_SetString(PyExc_ValueError, "NULL result where temporary expected");
        return nullptr;
    }

// fixme? - why doesn't this do the same as `self._get_smart_ptr().get()'
    ObjectProxy* pyobj = (ObjectProxy*)BindCppObject(
        (void*)GILCallR((Cppyy::TCppMethod_t)fDereferencer, value, ctxt), fRawPtrType);

    if (pyobj) {
        pyobj->SetSmartPtr((void*)value, fClass);
        pyobj->PythonOwns();  // life-time control by python ref-counting
    }

    return (PyObject*)pyobj;
}

PyObject* CPyCppyy::TCppObjectBySmartPtrPtrExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
    Cppyy::TCppObject_t value = GILCallR(method, self, ctxt);
    if (!value)
        return nullptr;

// todo: why doesn't this do the same as `self._get_smart_ptr().get()'
    ObjectProxy* pyobj = (ObjectProxy*) BindCppObject(
        (void*)GILCallR((Cppyy::TCppMethod_t)fDereferencer, value, ctxt), fRawPtrType);

    if (pyobj)
        pyobj->SetSmartPtr((void*)value, fClass);

    return (PyObject*)pyobj;
}

PyObject* CPyCppyy::TCppObjectBySmartPtrRefExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
    Cppyy::TCppObject_t value = GILCallR(method, self, ctxt);
    if (!value)
        return nullptr;

    //if ( ! fAssignable ) {

// fixme? - why doesn't this do the same as `self._get_smart_ptr().get()'
    ObjectProxy* pyobj = (ObjectProxy*)BindCppObject(
        (void*)GILCallR((Cppyy::TCppMethod_t)fDereferencer, value, ctxt), fRawPtrType);

    if (pyobj)
         pyobj->SetSmartPtr((void*)value, fClass);

    return (PyObject*)pyobj;

   // todo: assignment not done yet
   //
   /*} else {

     PyObject* result = BindCppObject( (void*)value, fClass );

   // this generic code is quite slow compared to its C++ equivalent ...
      PyObject* assign = PyObject_GetAttrString( result, const_cast< char* >( "__assign__" ) );
      if ( ! assign ) {
         PyErr_Clear();
         PyObject* descr = PyObject_Str( result );
         if ( descr && PyBytes_CheckExact( descr ) ) {
            PyErr_Format( PyExc_TypeError, "can not assign to return object (%s)",
                          PyBytes_AS_STRING( descr ) );
         } else {
            PyErr_SetString( PyExc_TypeError, "can not assign to result" );
         }
         Py_XDECREF( descr );
         Py_DECREF( result );
         Py_DECREF( fAssignable ); fAssignable = 0;
         return 0;
      }

      PyObject* res2 = PyObject_CallFunction( assign, const_cast< char* >( "O" ), fAssignable );


      Py_DECREF( assign );
      Py_DECREF( result );
      Py_DECREF( fAssignable ); fAssignable = 0;

      if ( res2 ) {
         Py_DECREF( res2 );             // typically, *this from operator=()
         Py_RETURN_NONE;
      }

      return 0;
   }
   */
}


//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TCppObjectArrayExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, construct TTupleOfInstances from
// return value
    return BindCppObjectArray((void*)GILCallR(method, self, ctxt), fClass, fArraySize);
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TConstructorExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t klass, TCallContext* ctxt)
{
// package return address in PyObject* for caller to handle appropriately (see
// TConstructorHolder for the actual build of the PyObject)
    return (PyObject*)GILCallConstructor(method, (Cppyy::TCppType_t)klass, ctxt);
}

//-----------------------------------------------------------------------------
PyObject* CPyCppyy::TPyObjectExecutor::Execute(
    Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, TCallContext* ctxt)
{
// execute <method> with argument <self, ctxt>, return python object
    return (PyObject*)GILCallR(method, self, ctxt);
}


//- factories -----------------------------------------------------------------
CPyCppyy::TExecutor* CPyCppyy::CreateExecutor(
    const std::string& fullType, bool manage_smart_ptr)
{
// The matching of the fulltype to an executor factory goes through up to 4 levels:
//   1) full, qualified match
//   2) drop '&' as by ref/full type is often pretty much the same python-wise
//   3) C++ classes, either by ref/ptr or by value
//   4) additional special case for enums
//
// If all fails, void is used, which will cause the return type to be ignored on use

// an exactly matching executor is best
    ExecFactories_t::iterator h = gExecFactories.find(fullType);
    if (h != gExecFactories.end())
        return (h->second)();

// resolve typedefs etc.
    const std::string& resolvedType = Cppyy::ResolveName(fullType);

// a full, qualified matching executor is preferred
    if (resolvedType != fullType) {
         h = gExecFactories.find(resolvedType);
         if (h != gExecFactories.end())
              return (h->second)();
    }

//-- nothing? ok, collect information about the type and possible qualifiers/decorators
    bool isConst = strncmp(resolvedType.c_str(), "const", 5)  == 0;
    const std::string& cpd = Utility::Compound(resolvedType);
    std::string realType = TypeManip::clean_type(resolvedType, false);

// accept unqualified type (as python does not know about qualifiers)
    h = gExecFactories.find(realType + cpd);
    if (h != gExecFactories.end())
        return (h->second)();

// drop const, as that is mostly meaningless to python (with the exception
// of c-strings, but those are specialized in the converter map)
    if (isConst) {
        realType = TypeManip::remove_const(realType);
        h = gExecFactories.find(realType + cpd);
        if (h != gExecFactories.end())
            return (h->second)();
    }

//-- still nothing? try pointer instead of array (for builtins)
    if (cpd == "[]") {
    /* // CLING WORKAROUND -- if the type is a fixed-size array, it will have a funky
    // resolved type like MyClass(&)[N], which TClass::GetClass() fails on. So, strip
    // it down:
        realType = TClassEdit::CleanType( realType.substr( 0, realType.rfind("(") ).c_str(), 1 );
    // -- CLING WORKAROUND */
        h = gExecFactories.find(realType + "*");
        if (h != gExecFactories.end())
            return (h->second)();           // TODO: use array size
    }

// C++ classes and special cases (enum)
    TExecutor* result = 0;
    if (Cppyy::TCppType_t klass = Cppyy::GetScope(realType)) {
        if (manage_smart_ptr && Cppyy::IsSmartPtr(realType)) {
            const std::vector<Cppyy::TCppIndex_t> methods =
                Cppyy::GetMethodIndicesFromName(klass, "operator->");
            if (!methods.empty()) {
                Cppyy::TCppMethod_t method = Cppyy::GetMethod(klass, methods[0]);
                Cppyy::TCppType_t rawPtrType = Cppyy::GetScope(
                    TypeManip::clean_type(Cppyy::GetMethodResultType(method)));
                if (rawPtrType) {
                    if (cpd == "") {
                        result = new TCppObjectBySmartPtrExecutor(klass, rawPtrType, method);
                    } else if (cpd == "*") {
                        result = new TCppObjectBySmartPtrPtrExecutor(klass, rawPtrType, method);
                    } else if (cpd == "&") {
                        result = new TCppObjectBySmartPtrRefExecutor(klass, rawPtrType, method);
                    } // else if (cpd == "**") {
                //  } else if (cpd == "*&" || cpd == "&*") {
                //  } else if (cpd == "[]") {
                //  } else {
                }
            }
        }

        if (!result) {
            if (cpd == "")
                result = new TCppObjectByValueExecutor(klass);
            else if (cpd == "&")
                result = new TCppObjectRefExecutor(klass);
            else if (cpd == "**")
                result = new TCppObjectPtrPtrExecutor(klass);
            else if (cpd == "*&"|| cpd == "&*" )
                result = new TCppObjectPtrRefExecutor(klass);
            else if (cpd == "[]") {
                Py_ssize_t asize = Utility::ArraySize(resolvedType);
                if (0 < asize)
                    result = new TCppObjectArrayExecutor(klass, asize);
                else
                    result = new TCppObjectPtrRefExecutor(klass);
            } else
                result = new TCppObjectExecutor(klass);
        }
    } else {
    // unknown: void* may work ("user knows best"), void will fail on use of return value
        h = (cpd == "") ? gExecFactories.find("void") : gExecFactories.find("void*");
    }

    if (!result && h != gExecFactories.end())
    // executor factory available, use it to create executor
        result = (h->second)();

   return result;                  // may still be null
}


//-----------------------------------------------------------------------------
#define CPPYY_EXECUTOR_FACTORY(name)                                          \
TExecutor* Create##name##Executor()                                           \
{                                                                             \
    return new T##name##Executor;                                             \
}

namespace {

    using namespace CPyCppyy;

// use macro rather than template for portability ...
    CPPYY_EXECUTOR_FACTORY(Bool)
    CPPYY_EXECUTOR_FACTORY(BoolRef)
    CPPYY_EXECUTOR_FACTORY(BoolConstRef)
    CPPYY_EXECUTOR_FACTORY(Char)
    CPPYY_EXECUTOR_FACTORY(CharRef)
    CPPYY_EXECUTOR_FACTORY(CharConstRef)
    CPPYY_EXECUTOR_FACTORY(UChar)
    CPPYY_EXECUTOR_FACTORY(UCharRef)
    CPPYY_EXECUTOR_FACTORY(UCharConstRef)
    CPPYY_EXECUTOR_FACTORY(Short)
    CPPYY_EXECUTOR_FACTORY(ShortRef)
    CPPYY_EXECUTOR_FACTORY(UShortRef)
    CPPYY_EXECUTOR_FACTORY(Int)
    CPPYY_EXECUTOR_FACTORY(IntRef)
    CPPYY_EXECUTOR_FACTORY(UIntRef)
    CPPYY_EXECUTOR_FACTORY(ULong)
    CPPYY_EXECUTOR_FACTORY(ULongRef)
    CPPYY_EXECUTOR_FACTORY(Long)
    CPPYY_EXECUTOR_FACTORY(LongRef)
    CPPYY_EXECUTOR_FACTORY(Float)
    CPPYY_EXECUTOR_FACTORY(FloatRef)
    CPPYY_EXECUTOR_FACTORY(Double)
    CPPYY_EXECUTOR_FACTORY(DoubleRef)
    CPPYY_EXECUTOR_FACTORY(LongDouble)
    CPPYY_EXECUTOR_FACTORY(LongDoubleRef)
    CPPYY_EXECUTOR_FACTORY(Void)
    CPPYY_EXECUTOR_FACTORY(LongLong)
    CPPYY_EXECUTOR_FACTORY(LongLongRef)
    CPPYY_EXECUTOR_FACTORY(ULongLong)
    CPPYY_EXECUTOR_FACTORY(ULongLongRef)
    CPPYY_EXECUTOR_FACTORY(CString)
    CPPYY_EXECUTOR_FACTORY(VoidArray)
    CPPYY_EXECUTOR_FACTORY(BoolArray)
    CPPYY_EXECUTOR_FACTORY(ShortArray)
    CPPYY_EXECUTOR_FACTORY(UShortArray)
    CPPYY_EXECUTOR_FACTORY(IntArray)
    CPPYY_EXECUTOR_FACTORY(UIntArray)
    CPPYY_EXECUTOR_FACTORY(LongArray)
    CPPYY_EXECUTOR_FACTORY(ULongArray)
    CPPYY_EXECUTOR_FACTORY(FloatArray)
    CPPYY_EXECUTOR_FACTORY(DoubleArray)
    CPPYY_EXECUTOR_FACTORY(STLString)
    CPPYY_EXECUTOR_FACTORY(STLStringRef)
    CPPYY_EXECUTOR_FACTORY(Constructor)
    CPPYY_EXECUTOR_FACTORY(PyObject)

// executor factories for C++ types
    typedef std::pair< const char*, ExecutorFactory_t > NFp_t;

    NFp_t factories_[] = {
    // factories for built-ins
        NFp_t("bool",               &CreateBoolExecutor               ),
        NFp_t("bool&",              &CreateBoolRefExecutor            ),
        NFp_t("const bool&",        &CreateBoolConstRefExecutor       ),
        NFp_t("char",               &CreateCharExecutor               ),
        NFp_t("signed char",        &CreateCharExecutor               ),
        NFp_t("unsigned char",      &CreateUCharExecutor              ),
        NFp_t("char&",              &CreateCharRefExecutor            ),
        NFp_t("signed char&",       &CreateCharRefExecutor            ),
        NFp_t("unsigned char&",     &CreateUCharRefExecutor           ),
        NFp_t("const char&",        &CreateCharConstRefExecutor       ),
        NFp_t("const signed char&", &CreateCharConstRefExecutor       ),
        NFp_t("const unsigned char&", &CreateUCharConstRefExecutor    ),
        NFp_t("short",              &CreateShortExecutor              ),
        NFp_t("short&",             &CreateShortRefExecutor           ),
        NFp_t("unsigned short",     &CreateIntExecutor                ),
        NFp_t("unsigned short&",    &CreateUShortRefExecutor          ),
        NFp_t("int",                &CreateIntExecutor                ),
        NFp_t("int&",               &CreateIntRefExecutor             ),
        NFp_t("unsigned int",       &CreateULongExecutor              ),
        NFp_t("unsigned int&",      &CreateUIntRefExecutor            ),
        NFp_t("internal_enum_type_t",  &CreateULongExecutor           ),
        NFp_t("internal_enum_type_t&", &CreateUIntRefExecutor         ),
        NFp_t("long",               &CreateLongExecutor               ),
        NFp_t("long&",              &CreateLongRefExecutor            ),
        NFp_t("unsigned long",      &CreateULongExecutor              ),
        NFp_t("unsigned long&",     &CreateULongRefExecutor           ),
        NFp_t("long long",          &CreateLongLongExecutor           ),
        NFp_t("Long64_t",           &CreateLongLongExecutor           ),
        NFp_t("long long&",         &CreateLongLongRefExecutor        ),
        NFp_t("Long64_t&",          &CreateLongLongRefExecutor        ),
        NFp_t("unsigned long long", &CreateULongLongExecutor          ),
        NFp_t("ULong64_t",          &CreateULongLongExecutor          ),
        NFp_t("unsigned long long&", &CreateULongLongRefExecutor      ),
        NFp_t("ULong64_t&",         &CreateULongLongRefExecutor       ),

        NFp_t("float",              &CreateFloatExecutor              ),
        NFp_t("float&",             &CreateFloatRefExecutor           ),
        NFp_t("Float16_t",          &CreateFloatExecutor              ),
        NFp_t("Float16_t&",         &CreateFloatRefExecutor           ),
        NFp_t("double",             &CreateDoubleExecutor             ),
        NFp_t("double&",            &CreateDoubleRefExecutor          ),
        NFp_t("Double32_t",         &CreateDoubleExecutor             ),
        NFp_t("Double32_t&",        &CreateDoubleRefExecutor          ),
        NFp_t("long double",        &CreateLongDoubleExecutor         ),   // TODO: lost precision
        NFp_t("long double&",       &CreateLongDoubleRefExecutor      ),
        NFp_t("void",               &CreateVoidExecutor               ),

   // pointer/array factories
        NFp_t("void*",              &CreateVoidArrayExecutor          ),
        NFp_t("bool*",              &CreateBoolArrayExecutor          ),
        NFp_t("short*",             &CreateShortArrayExecutor         ),
        NFp_t("unsigned short*",    &CreateUShortArrayExecutor        ),
        NFp_t("int*",               &CreateIntArrayExecutor           ),
        NFp_t("unsigned int*",      &CreateUIntArrayExecutor          ),
        NFp_t("internal_enum_type_t*", &CreateUIntArrayExecutor       ),
        NFp_t("long*",              &CreateLongArrayExecutor          ),
        NFp_t("unsigned long*",     &CreateULongArrayExecutor         ),
        NFp_t("float*",             &CreateFloatArrayExecutor         ),
        NFp_t("double*",            &CreateDoubleArrayExecutor        ),

   // factories for special cases
        NFp_t("const char*",        &CreateCStringExecutor            ),
        NFp_t("char*",              &CreateCStringExecutor            ),
        NFp_t("std::string",        &CreateSTLStringExecutor          ),
        NFp_t("string",             &CreateSTLStringExecutor          ),
        NFp_t("std::string&",       &CreateSTLStringRefExecutor       ),
        NFp_t("string&",            &CreateSTLStringRefExecutor       ),
        NFp_t("__init__",           &CreateConstructorExecutor        ),
        NFp_t("PyObject*",          &CreatePyObjectExecutor           ),
        NFp_t("_object*",           &CreatePyObjectExecutor           ),
        NFp_t("FILE*",              &CreateVoidArrayExecutor          )
    };

    struct InitExecFactories_t {
    public:
        InitExecFactories_t() {
        // load all executor factories in the global map 'gExecFactories'
            int nf = sizeof(factories_)/sizeof(factories_[0]);
            for (int i = 0; i < nf; ++i) {
                gExecFactories[factories_[i].first] = factories_[i].second;
            }
        }
    } initExecvFactories_;

} // unnamed namespace
