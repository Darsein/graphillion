/*********************************************************************
Copyright 2013  JST ERATO Minato project and other contributors
http://www-erato.ist.hokudai.ac.jp/?language=en

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**********************************************************************/

#include <Python.h>
#include "structmember.h"

#include "pygraphillion.h"

#include <cstdlib>
#include <climits>

#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>

#include "graphillion/graphset.h"

using graphillion::setset;
using graphillion::Range;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::stringstream;
using std::vector;

#define CHECK_OR_ERROR(obj, check, name, ret)         \
  do {                                                \
    if (!check(obj)) {                                \
      PyErr_SetString(PyExc_TypeError, "not " name);  \
      return (ret);                                   \
    }                                                 \
  } while (0) ;

#define CHECK_SETSET_OR_ERROR(obj)                              \
  CHECK_OR_ERROR(obj, PySetset_Check, "setset", NULL);

#define RETURN_NEW_SETSET(self, expr)                         \
  do {                                                        \
    PySetsetObject* _ret = reinterpret_cast<PySetsetObject*>( \
        (self)->ob_type->tp_alloc((self)->ob_type, 0));       \
    _ret->ss = new setset(expr);                              \
    return reinterpret_cast<PyObject*>(_ret);                 \
  } while (0);


#define RETURN_NEW_SETSET2(self, other, _other, expr)                   \
  do {                                                                  \
    PySetsetObject* (_other) = reinterpret_cast<PySetsetObject*>(other); \
    PySetsetObject* _ret = reinterpret_cast<PySetsetObject*>(           \
        (self)->ob_type->tp_alloc((self)->ob_type, 0));                 \
    if (_ret == NULL) return NULL;                                      \
    _ret->ss = new setset(expr);                                        \
    return reinterpret_cast<PyObject*>(_ret);                           \
  } while (0);

#define RETURN_SELF_SETSET(self, other, _other, expr)                  \
  do {                                                                 \
    PySetsetObject* _other = reinterpret_cast<PySetsetObject*>(other); \
    (expr);                                                            \
    Py_INCREF(self);                                                   \
    return reinterpret_cast<PyObject*>(self);                          \
  } while (0);

#define RETURN_TRUE_IF(self, other, _other, expr)                       \
  do {                                                                  \
    PySetsetObject* (_other) = reinterpret_cast<PySetsetObject*>(other); \
    if (expr) Py_RETURN_TRUE;                                           \
    else      Py_RETURN_FALSE;                                          \
  } while (0);

#define DO_FOR_MULTI(self, others, expr)                                \
  do {                                                                  \
    PyObject* _result = reinterpret_cast<PyObject*>(self);              \
    if (PyTuple_GET_SIZE(others) == 0)                                  \
      return setset_copy(self);                                         \
    Py_INCREF(self);                                                    \
    for (Py_ssize_t _i = 0; _i < PyTuple_GET_SIZE(others); ++_i) {      \
      PyObject* _other = PyTuple_GET_ITEM(others, _i);                  \
      PyObject* _newresult                                              \
          = expr(reinterpret_cast<PySetsetObject*>(_result), _other);   \
      if (_newresult == NULL) {                                         \
        Py_DECREF(_result);                                             \
        return NULL;                                                    \
      }                                                                 \
      Py_DECREF(_result);                                               \
      _result = _newresult;                                             \
    }                                                                   \
    return _result;                                                     \
  } while (0);

#define UPDATE_FOR_MULTI(self, others, expr)                           \
  do {                                                                 \
    for (Py_ssize_t _i = 0; _i < PyTuple_GET_SIZE(others); ++_i) {     \
      PyObject* _other = PyTuple_GET_ITEM(others, _i);                 \
      if (expr(self, _other) == NULL)                                  \
        return NULL;                                                   \
    }                                                                  \
    Py_RETURN_NONE;                                                    \
  } while (0);

static PyObject* setset_build_set(const set<int>& s) {
  PyObject* so = PySet_New(NULL);
  for (set<int>::const_iterator e = s.begin(); e != s.end(); ++e) {
    PyObject* eo = PyInt_FromLong(*e);
    if (eo == NULL) {
      PyErr_SetString(PyExc_TypeError, "not int set");
      Py_DECREF(eo);
      return NULL;
    }
    if (PySet_Add(so, eo) == -1) {
      PyErr_SetString(PyExc_RuntimeError, "can't add elements to a set");
      Py_DECREF(eo);
      return NULL;
    }
    Py_DECREF(eo);  // TODO: is Py_DECREF() required for PyInt_FromLong object?
  }
  return so;
}

static int setset_parse_set(PyObject* so, set<int>* s) {
  assert(s != NULL);
  PyObject* i = PyObject_GetIter(so);
  if (i == NULL) return -1;
  PyObject* eo;
  while ((eo = PyIter_Next(i))) {
    if (!PyInt_Check(eo)) {
      Py_DECREF(eo);
      PyErr_SetString(PyExc_TypeError, "not int set");
      return -1;
    }
    s->insert(PyInt_AsLong(eo));
    Py_DECREF(eo);
  }
  Py_DECREF(i);
  return 0;
}

static int setset_parse_map(PyObject* dict_obj, map<string, vector<int> >* m) {
  assert(m != NULL);
  PyObject* key_obj;
  PyObject* lo;
  Py_ssize_t pos = 0;
  while (PyDict_Next(dict_obj, &pos, &key_obj, &lo)) {
    if (!PyString_Check(key_obj)) {
      PyErr_SetString(PyExc_TypeError, "invalid argument");
      return -1;
    }
    string key = PyString_AsString(key_obj);
    PyObject* i = PyObject_GetIter(lo);
    if (i == NULL) return -1;
    vector<int> v;
    PyObject* eo;
    while ((eo = PyIter_Next(i))) {
      if (!PyInt_Check(eo)) {
        Py_DECREF(eo);
        PyErr_SetString(PyExc_TypeError, "not int");
        return -1;
      }
      v.push_back(PyInt_AsLong(eo));
      Py_DECREF(eo);
    }
    Py_DECREF(i);
    (*m)[key] = v;
  }
  return 0;
}

// setset::iterator

typedef struct {
  PyObject_HEAD
  setset::iterator* it;
} PySetsetIterObject;

static PyObject* setsetiter_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
  PySetsetIterObject* self;
  self = reinterpret_cast<PySetsetIterObject*>(type->tp_alloc(type, 0));
  if (self == NULL) return NULL;
  return reinterpret_cast<PyObject*>(self);
}

static void setsetiter_dealloc(PySetsetIterObject* self) {
  delete self->it;
  PyObject_Del(self);
}

static PyObject* setsetiter_next(PySetsetIterObject* self) {
  if (*(self->it) == setset::end())
    return NULL;
  set<int> s = *(*self->it);
  ++(*self->it);
  return setset_build_set(s);
}

static PyMethodDef setsetiter_methods[] = {
  {NULL,           NULL}           /* sentinel */
};

static PyTypeObject PySetsetIter_Type = {
  PyVarObject_HEAD_INIT(&PyType_Type, 0)
  "setset_iterator",                          /* tp_name */
  sizeof(PySetsetIterObject),                 /* tp_basicsize */
  0,                                          /* tp_itemsize */
  /* methods */
  reinterpret_cast<destructor>(setsetiter_dealloc), /* tp_dealloc */
  0,                                          /* tp_print */
  0,                                          /* tp_getattr */
  0,                                          /* tp_setattr */
  0,                                          /* tp_compare */
  0,                                          /* tp_repr */
  0,                                          /* tp_as_number */
  0,                                          /* tp_as_sequence */
  0,                                          /* tp_as_mapping */
  0,                                          /* tp_hash */
  0,                                          /* tp_call */
  0,                                          /* tp_str */
  PyObject_GenericGetAttr,                    /* tp_getattro */
  0,                                          /* tp_setattro */
  0,                                          /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_ITER, /* tp_flags */
  0,                                          /* tp_doc */
  0,                                          /* tp_traverse */
  0,                                          /* tp_clear */
  0,                                          /* tp_richcompare */
  0,                                          /* tp_weaklistoffset */
  PyObject_SelfIter,                          /* tp_iter */
  reinterpret_cast<iternextfunc>(setsetiter_next), /* tp_iternext */
  setsetiter_methods,                         /* tp_methods */
  0,                                          /* tp_members */
  0,                                          /* tp_getset */
  0,                                          /* tp_base */
  0,                                          /* tp_dict */
  0,                                          /* tp_descr_get */
  0,                                          /* tp_descr_set */
  0,                                          /* tp_dictoffset */
  0,                                          /* tp_init */
  PyType_GenericAlloc,                        /* tp_alloc */
  setsetiter_new                              /* tp_new */
};

// setset

static PyObject* setset_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
  PySetsetObject* self;
  self = reinterpret_cast<PySetsetObject*>(type->tp_alloc(type, 0));
  if (self == NULL) return NULL;
  return reinterpret_cast<PyObject*>(self);
}

static int setset_init(PySetsetObject* self, PyObject* args, PyObject* kwds) {
  PyObject* obj = NULL;
  if (!PyArg_ParseTuple(args, "|O", &obj))
    return -1;
  if (obj == NULL || obj == Py_None) {
    self->ss = new setset();
  } else if (PySetset_Check(obj)) {
    PySetsetObject* sso = reinterpret_cast<PySetsetObject*>(obj);
    self->ss = new setset(*(sso->ss));
  } else if (PyList_Check(obj)) {
    PyObject* i = PyObject_GetIter(obj);
    if (i == NULL) return -1;
    vector<set<int> > vs;
    PyObject* o;
    while ((o = PyIter_Next(i))) {
      if (!PyAnySet_Check(o)) {
        PyErr_SetString(PyExc_TypeError, "not set");
        return -1;
      }
      set<int> s;
      if (setset_parse_set(o, &s) == -1) return -1;
      vs.push_back(s);
      Py_DECREF(o);
    }
    Py_DECREF(i);
    self->ss = new setset(vs);
  } else if (PyDict_Check(obj)) {
    map<string, vector<int> > m;
    if (setset_parse_map(obj, &m) == -1) return -1;
    self->ss = new setset(m);
  } else {
    PyErr_SetString(PyExc_TypeError, "invalid argumet");
    return -1;
  }
  if (PyErr_Occurred())
    return -1;
  return 0;
}

static void setset_dealloc(PySetsetObject* self) {
  delete self->ss;
  self->ob_type->tp_free(reinterpret_cast<PyObject*>(self));
}

static PyObject* setset_copy(PySetsetObject* self) {
  RETURN_NEW_SETSET(self, *self->ss);
}

static PyObject* setset_invert(PySetsetObject* self) {
  RETURN_NEW_SETSET(self, ~(*self->ss));
}

static PyObject* setset_union(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, (*self->ss) | (*_other->ss));
}

static PyObject* setset_union_multi(PySetsetObject* self, PyObject* others) {
  DO_FOR_MULTI(self, others, setset_union);
}

static PyObject* setset_update(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_SELF_SETSET(self, other, _other, (*self->ss) |= (*_other->ss));
}

static PyObject* setset_update_multi(PySetsetObject* self, PyObject* others) {
  UPDATE_FOR_MULTI(self, others, setset_update);
}

static PyObject* setset_intersection(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, (*self->ss) & (*_other->ss));
}

static PyObject* setset_intersection_multi(PySetsetObject* self, PyObject* others) {
  DO_FOR_MULTI(self, others, setset_intersection);
}

static PyObject* setset_intersection_update(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_SELF_SETSET(self, other, _other, (*self->ss) &= (*_other->ss));
}

static PyObject* setset_intersection_update_multi(PySetsetObject* self, PyObject* others) {
  UPDATE_FOR_MULTI(self, others, setset_intersection_update);
}

static PyObject* setset_difference(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, (*self->ss) - (*_other->ss));
}

static PyObject* setset_difference_multi(PySetsetObject* self, PyObject* others) {
  DO_FOR_MULTI(self, others, setset_difference);
}

static PyObject* setset_difference_update(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_SELF_SETSET(self, other, _other, (*self->ss) -= (*_other->ss));
}

static PyObject* setset_difference_update_multi(PySetsetObject* self, PyObject* others) {
  UPDATE_FOR_MULTI(self, others, setset_difference_update);
}

static PyObject* setset_symmetric_difference(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, (*self->ss) ^ (*_other->ss));
}

static PyObject* setset_symmetric_difference_multi(PySetsetObject* self, PyObject* others) {
  DO_FOR_MULTI(self, others, setset_symmetric_difference);
}

static PyObject* setset_symmetric_difference_update(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_SELF_SETSET(self, other, _other, (*self->ss) ^= (*_other->ss));
}

static PyObject* setset_symmetric_difference_update_multi(PySetsetObject* self, PyObject* others) {
  UPDATE_FOR_MULTI(self, others, setset_symmetric_difference_update);
}

static PyObject* setset_quotient(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, (*self->ss) / (*_other->ss));
}

static PyObject* setset_quotient_update(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_SELF_SETSET(self, other, _other, (*self->ss) /= (*_other->ss));
}

static PyObject* setset_remainder(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, (*self->ss) % (*_other->ss));
}

static PyObject* setset_remainder_update(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_SELF_SETSET(self, other, _other, (*self->ss) %= (*_other->ss));
}

static PyObject* setset_isdisjoint(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_TRUE_IF(self, other, _other, self->ss->is_disjoint(*_other->ss));
}

static PyObject* setset_issubset(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_TRUE_IF(self, other, _other, self->ss->is_subset(*_other->ss));
}

static PyObject* setset_issuperset(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_TRUE_IF(self, other, _other, self->ss->is_superset(*_other->ss));
}

static int setset_nonzero(PySetsetObject* self) {
  return !self->ss->empty();
}

static Py_ssize_t setset_len(PyObject* obj) {
  PySetsetObject* self = reinterpret_cast<PySetsetObject*>(obj);
  long long int len = strtoll(self->ss->size().c_str(), NULL, 0);
  if (len != LLONG_MAX) {
    return len;
  } else {
    PyErr_SetString(PyExc_OverflowError, "overflow, use obj.len()");
    return -1;
  }
}

static PyObject* setset_long_len(PyObject* obj) {
  PySetsetObject* self = reinterpret_cast<PySetsetObject*>(obj);
  vector<char> buf;
  string size = self->ss->size();
  for (string::const_iterator c = size.begin(); c != size.end(); ++c)
    buf.push_back(*c);
  buf.push_back('\0');
#ifdef HAVE_LIBGMPXX
  return PyLong_FromString(buf.data(), NULL, 0);
#else
  return PyLong_FromDouble(strtod(buf.data(), NULL));
#endif
}

static PyObject* setset_iter(PySetsetObject* self) {
  PySetsetIterObject* ssi = PyObject_New(PySetsetIterObject, &PySetsetIter_Type);
  if (ssi == NULL) return NULL;
  ssi->it = new setset::iterator(self->ss->begin());
  if (ssi->it == NULL) {
    PyErr_NoMemory();
    return NULL;
  }
  return reinterpret_cast<PyObject*>(ssi);
}

static PyObject* setset_randomize(PySetsetObject* self) {
  PySetsetIterObject* ssi = PyObject_New(PySetsetIterObject, &PySetsetIter_Type);
  if (ssi == NULL) return NULL;
  ssi->it = new setset::random_iterator(self->ss->randomize());
  if (ssi->it == NULL) {
    PyErr_NoMemory();
    return NULL;
  }
  return reinterpret_cast<PyObject*>(ssi);
}

static PyObject* setset_optimize(PySetsetObject* self, PyObject* weights,
                                 bool is_maximizing) {
  PyObject* i = PyObject_GetIter(weights);
  if (i == NULL) return NULL;
  PyObject* eo;
  vector<double> w;
  while ((eo = PyIter_Next(i))) {
    if (PyFloat_Check(eo)) {
      w.push_back(PyFloat_AsDouble(eo));
    }
    else if (PyLong_Check(eo)) {
      w.push_back(static_cast<double>(PyLong_AsLong(eo)));
    }
    else if (PyInt_Check(eo)) {
      w.push_back(static_cast<double>(PyInt_AsLong(eo)));
    }
    else {
      PyErr_SetString(PyExc_TypeError, "not a number");
      Py_DECREF(eo);
      return NULL;
    }
    Py_DECREF(eo);
  }
  Py_DECREF(i);
  PySetsetIterObject* ssi = PyObject_New(PySetsetIterObject, &PySetsetIter_Type);
  if (ssi == NULL) return NULL;
  ssi->it = new setset::weighted_iterator(
      is_maximizing ? self->ss->maximize(w) : self->ss->minimize(w));
  if (ssi->it == NULL) {
    PyErr_NoMemory();
    return NULL;
  }
  return reinterpret_cast<PyObject*>(ssi);
}

static PyObject* setset_maximize(PySetsetObject* self, PyObject* weights) {
  return setset_optimize(self, weights, true);
}

static PyObject* setset_minimize(PySetsetObject* self, PyObject* weights) {
  return setset_optimize(self, weights, false);
}

// If an item in o is equal to value, return 1, otherwise return 0. On error, return -1.
static int setset_contains(PySetsetObject* self, PyObject* so) {
  CHECK_OR_ERROR(so, PyAnySet_Check, "set", -1);
  set<int> s;
  if (setset_parse_set(so, &s) == -1) return -1;
  return self->ss->find(s) != setset::end() ? 1 : 0;
}

static PyObject* setset_include(PySetsetObject* self, PyObject* eo) {
  CHECK_OR_ERROR(eo, PyInt_Check, "int", NULL);
  PySetsetObject* sso = reinterpret_cast<PySetsetObject*>(
      self->ob_type->tp_alloc(self->ob_type, 0));
  sso->ss = new setset(self->ss->include(PyInt_AsLong(eo)));
  return reinterpret_cast<PyObject*>(sso);
}

static PyObject* setset_exclude(PySetsetObject* self, PyObject* eo) {
  CHECK_OR_ERROR(eo, PyInt_Check, "int", NULL);
  PySetsetObject* sso = reinterpret_cast<PySetsetObject*>(
      self->ob_type->tp_alloc(self->ob_type, 0));
  sso->ss = new setset(self->ss->exclude(PyInt_AsLong(eo)));
  return reinterpret_cast<PyObject*>(sso);
}

static PyObject* setset_add(PySetsetObject* self, PyObject* obj) {
  if (PyAnySet_Check(obj)) {
    set<int> s;
    if (setset_parse_set(obj, &s) == -1) return NULL;
    self->ss->insert(s);
  } else if (PyInt_Check(obj)) {
    int e = PyLong_AsLong(obj);
    self->ss->insert(e);
  } else {
    PyErr_SetString(PyExc_TypeError, "not set nor int");
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* setset_remove(PySetsetObject* self, PyObject* obj) {
  if (PyAnySet_Check(obj)) {
    set<int> s;
    if (setset_parse_set(obj, &s) == -1) return NULL;
    if (self->ss->erase(s) == 0) {
      PyErr_SetString(PyExc_KeyError, "not found");
      return NULL;
    }
    self->ss->erase(s);
  } else if (PyInt_Check(obj)) {
    int e = PyLong_AsLong(obj);
    if (self->ss->include(e).empty()) {
      PyErr_SetString(PyExc_KeyError, "not found");
      return NULL;
    }
    self->ss->erase(e);
  } else {
    PyErr_SetString(PyExc_TypeError, "not set nor int");
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* setset_discard(PySetsetObject* self, PyObject* obj) {
  if (PyAnySet_Check(obj)) {
    set<int> s;
    if (setset_parse_set(obj, &s) == -1) return NULL;
    self->ss->erase(s);
  } else if (PyInt_Check(obj)) {
    int e = PyLong_AsLong(obj);
    self->ss->erase(e);
  } else {
    PyErr_SetString(PyExc_TypeError, "not set nor int");
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* setset_pop(PySetsetObject* self) {
  setset::iterator i = self->ss->begin();
  if (i == setset::end()) {
    PyErr_SetString(PyExc_KeyError, "not found");
    return NULL;
  }
  set<int> s = *i;
  self->ss->erase(s);
  return setset_build_set(s);
}

static PyObject* setset_clear(PySetsetObject* self) {
  self->ss->clear();
  Py_RETURN_NONE;
}

static PyObject* setset_minimal(PySetsetObject* self) {
  RETURN_NEW_SETSET(self, self->ss->minimal());
}

static PyObject* setset_maximal(PySetsetObject* self) {
  RETURN_NEW_SETSET(self, self->ss->maximal());
}

static PyObject* setset_hitting(PySetsetObject* self) {
  RETURN_NEW_SETSET(self, self->ss->hitting());
}

static PyObject* setset_smaller(PySetsetObject* self, PyObject* io) {
  CHECK_OR_ERROR(io, PyInt_Check, "int", NULL);
  int set_size = PyLong_AsLong(io);
  if (set_size < 0) {
    PyErr_SetString(PyExc_ValueError, "not unsigned int");
    return NULL;
  }
  RETURN_NEW_SETSET(self, self->ss->smaller(set_size));
}

static PyObject* setset_larger(PySetsetObject* self, PyObject* io) {
  CHECK_OR_ERROR(io, PyInt_Check, "int", NULL);
  int set_size = PyLong_AsLong(io);
  if (set_size < 0) {
    PyErr_SetString(PyExc_ValueError, "not unsigned int");
    return NULL;
  }
  RETURN_NEW_SETSET(self, self->ss->larger(set_size));
}

static PyObject* setset_same_len(PySetsetObject* self, PyObject* io) {
  CHECK_OR_ERROR(io, PyInt_Check, "int", NULL);
  int set_size = PyLong_AsLong(io);
  if (set_size < 0) {
    PyErr_SetString(PyExc_ValueError, "not unsigned int");
    return NULL;
  }
  RETURN_NEW_SETSET(self, self->ss->same_size(set_size));
}

static PyObject* setset_flip(PySetsetObject* self, PyObject* args) {
  PyObject* obj = NULL;
  if (!PyArg_ParseTuple(args, "|O", &obj)) return NULL;
  if (obj == NULL || obj == Py_None) {
    RETURN_NEW_SETSET(self, self->ss->flip());
  } else if (PyInt_Check(obj)) {
    int e = PyLong_AsLong(obj);
    RETURN_NEW_SETSET(self, self->ss->flip(e));
  }
  PyErr_SetString(PyExc_TypeError, "not int");
  return NULL;
}

static PyObject* setset_join(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, self->ss->join(*_other->ss));
}

static PyObject* setset_meet(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, self->ss->meet(*_other->ss));
}

static PyObject* setset_subsets(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, self->ss->subsets(*_other->ss));
}

static PyObject* setset_supersets(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, self->ss->supersets(*_other->ss));
}

static PyObject* setset_non_subsets(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, self->ss->non_subsets(*_other->ss));
}

static PyObject* setset_non_supersets(PySetsetObject* self, PyObject* other) {
  CHECK_SETSET_OR_ERROR(other);
  RETURN_NEW_SETSET2(self, other, _other, self->ss->non_supersets(*_other->ss));
}

static PyObject* setset_dump(PySetsetObject* self, PyObject* obj) {
  CHECK_OR_ERROR(obj, PyFile_Check, "file", NULL);
  FILE* fp = PyFile_AsFile(obj);
  PyFileObject* file = reinterpret_cast<PyFileObject*>(obj);
  PyFile_IncUseCount(file);
  Py_BEGIN_ALLOW_THREADS;
  self->ss->dump(fp);
  Py_END_ALLOW_THREADS;
  PyFile_DecUseCount(file);
  Py_RETURN_NONE;
}

static PyObject* setset_dumps(PySetsetObject* self) {
  stringstream sstr;
  self->ss->dump(sstr);
  return PyString_FromString(sstr.str().c_str());
}

static PyObject* setset_load(PySetsetObject* self, PyObject* obj) {
  CHECK_OR_ERROR(obj, PyFile_Check, "file", NULL);
  FILE* fp = PyFile_AsFile(obj);
  PyFileObject* file = reinterpret_cast<PyFileObject*>(obj);
  PyFile_IncUseCount(file);
  Py_BEGIN_ALLOW_THREADS;
  self->ss->load(fp);
  Py_END_ALLOW_THREADS;
  PyFile_DecUseCount(file);
  Py_RETURN_NONE;
}

static PyObject* setset_loads(PySetsetObject* self, PyObject* obj) {
  CHECK_OR_ERROR(obj, PyString_Check, "str", NULL);
  stringstream sstr(PyString_AsString(obj));
  self->ss->load(sstr);
  Py_RETURN_NONE;
}

static PyObject* setset_enum(PySetsetObject* self, PyObject* obj) {
  CHECK_OR_ERROR(obj, PyFile_Check, "file", NULL);
  FILE* fp = PyFile_AsFile(obj);
  PyFileObject* file = reinterpret_cast<PyFileObject*>(obj);
  PyFile_IncUseCount(file);
  Py_BEGIN_ALLOW_THREADS;
  string name = self->ob_type->tp_name;
  self->ss->_enum(fp, std::make_pair((name + "([").c_str(), "])"),
                  std::make_pair("set([", "])"));
  Py_END_ALLOW_THREADS;
  PyFile_DecUseCount(file);
  Py_RETURN_NONE;
}

static PyObject* setset_enums(PySetsetObject* self) {
  stringstream sstr;
  string name = self->ob_type->tp_name;
  self->ss->_enum(sstr, std::make_pair((name + "([").c_str(), "])"),
                  std::make_pair("set([", "])"));
  return PyString_FromString(sstr.str().c_str());
}

static PyObject* setset_repr(PySetsetObject* self) {
  return PyString_FromFormat("<%s object of %p>", self->ob_type->tp_name,
                             reinterpret_cast<void*>(self->ss->id()));
}
/*
static long setset_hash(PyObject* self) {
  PySetsetObject* sso = reinterpret_cast<PySetsetObject*>(self);
  return sso->ss->id();
}
*/
static int setset_nocmp(PyObject* self, PyObject* other) {
  PyErr_SetString(PyExc_TypeError, "cannot compare using cmp()");
  return -1;
}

static PyObject* setset_richcompare(PySetsetObject* self, PyObject* obj, int op) {
  PySetsetObject* sso;
  if(!PySetset_Check(obj)) {
    if (op == Py_EQ) Py_RETURN_FALSE;
    if (op == Py_NE) Py_RETURN_TRUE;
    PyErr_SetString(PyExc_TypeError, "can only compare to set of sets");
    return NULL;
  }
  sso = reinterpret_cast<PySetsetObject*>(obj);
  switch (op) {
    case Py_EQ:
      if (*self->ss == *sso->ss) Py_RETURN_TRUE;
      else Py_RETURN_FALSE;
    case Py_NE:
      if (*self->ss != *sso->ss) Py_RETURN_TRUE;
      else Py_RETURN_FALSE;
    case Py_LE:
      if (*self->ss <= *sso->ss) Py_RETURN_TRUE;
      else Py_RETURN_FALSE;
    case Py_GE:
      if (*self->ss >= *sso->ss) Py_RETURN_TRUE;
      else Py_RETURN_FALSE;
    case Py_LT:
      if (*self->ss < *sso->ss) Py_RETURN_TRUE;
      else Py_RETURN_FALSE;
    case Py_GT:
      if (*self->ss > *sso->ss) Py_RETURN_TRUE;
      else Py_RETURN_FALSE;
  }
  Py_INCREF(Py_NotImplemented);
  return Py_NotImplemented;
}

static PyMemberDef setset_members[] = {
  {NULL}  /* Sentinel */
};

static PyMethodDef setset_methods[] = {
  {"copy", reinterpret_cast<PyCFunction>(setset_copy), METH_NOARGS, ""},
  {"invert", reinterpret_cast<PyCFunction>(setset_invert), METH_NOARGS, ""},
  {"union", reinterpret_cast<PyCFunction>(setset_union_multi), METH_VARARGS, ""},
  {"update", reinterpret_cast<PyCFunction>(setset_update_multi), METH_VARARGS, ""},
  {"intersection", reinterpret_cast<PyCFunction>(setset_intersection_multi), METH_VARARGS, ""},
  {"intersection_update", reinterpret_cast<PyCFunction>(setset_intersection_update_multi), METH_VARARGS, ""},
  {"difference", reinterpret_cast<PyCFunction>(setset_difference_multi), METH_VARARGS, ""},
  {"difference_update", reinterpret_cast<PyCFunction>(setset_difference_update_multi), METH_VARARGS, ""},
  {"symmetric_difference", reinterpret_cast<PyCFunction>(setset_symmetric_difference_multi), METH_VARARGS, ""},
  {"symmetric_difference_update", reinterpret_cast<PyCFunction>(setset_symmetric_difference_update_multi), METH_VARARGS, ""},
  {"quotient", reinterpret_cast<PyCFunction>(setset_quotient), METH_O, ""},
  {"quotient_update", reinterpret_cast<PyCFunction>(setset_quotient_update), METH_O, ""},
  {"remainder", reinterpret_cast<PyCFunction>(setset_remainder), METH_O, ""},
  {"remainder_update", reinterpret_cast<PyCFunction>(setset_remainder_update), METH_O, ""},
  {"isdisjoint", reinterpret_cast<PyCFunction>(setset_isdisjoint), METH_O, ""},
  {"issubset", reinterpret_cast<PyCFunction>(setset_issubset), METH_O, ""},
  {"issuperset", reinterpret_cast<PyCFunction>(setset_issuperset), METH_O, ""},
  {"len", reinterpret_cast<PyCFunction>(setset_long_len), METH_NOARGS, ""},
  {"iter", reinterpret_cast<PyCFunction>(setset_iter), METH_NOARGS, ""},
  {"randomize", reinterpret_cast<PyCFunction>(setset_randomize), METH_NOARGS, ""},
  {"maximize", reinterpret_cast<PyCFunction>(setset_maximize), METH_O, ""},
  {"minimize", reinterpret_cast<PyCFunction>(setset_minimize), METH_O, ""},
  {"include", reinterpret_cast<PyCFunction>(setset_include), METH_O, ""},
  {"exclude", reinterpret_cast<PyCFunction>(setset_exclude), METH_O, ""},
  {"add", reinterpret_cast<PyCFunction>(setset_add), METH_O, ""},
  {"remove", reinterpret_cast<PyCFunction>(setset_remove), METH_O, ""},
  {"discard", reinterpret_cast<PyCFunction>(setset_discard), METH_O, ""},
  {"pop", reinterpret_cast<PyCFunction>(setset_pop), METH_NOARGS, ""},
  {"clear", reinterpret_cast<PyCFunction>(setset_clear), METH_NOARGS, ""},
  {"minimal", reinterpret_cast<PyCFunction>(setset_minimal), METH_NOARGS, ""},
  {"maximal", reinterpret_cast<PyCFunction>(setset_maximal), METH_NOARGS, ""},
  {"hitting", reinterpret_cast<PyCFunction>(setset_hitting), METH_NOARGS, ""},
  {"smaller", reinterpret_cast<PyCFunction>(setset_smaller), METH_O, ""},
  {"larger", reinterpret_cast<PyCFunction>(setset_larger), METH_O, ""},
  {"same_len", reinterpret_cast<PyCFunction>(setset_same_len), METH_O, ""},
  {"flip", reinterpret_cast<PyCFunction>(setset_flip), METH_VARARGS, ""},
  {"join", reinterpret_cast<PyCFunction>(setset_join), METH_O, ""},
  {"meet", reinterpret_cast<PyCFunction>(setset_meet), METH_O, ""},
  {"subsets", reinterpret_cast<PyCFunction>(setset_subsets), METH_O, ""},
  {"supersets", reinterpret_cast<PyCFunction>(setset_supersets), METH_O, ""},
  {"non_subsets", reinterpret_cast<PyCFunction>(setset_non_subsets), METH_O, ""},
  {"non_supersets", reinterpret_cast<PyCFunction>(setset_non_supersets), METH_O, ""},
  {"dump", reinterpret_cast<PyCFunction>(setset_dump), METH_O, ""},
  {"dumps", reinterpret_cast<PyCFunction>(setset_dumps), METH_NOARGS, ""},
  {"load", reinterpret_cast<PyCFunction>(setset_load), METH_O, ""},
  {"loads", reinterpret_cast<PyCFunction>(setset_loads), METH_O, ""},
  {"_enum", reinterpret_cast<PyCFunction>(setset_enum), METH_O, ""},
  {"_enums", reinterpret_cast<PyCFunction>(setset_enums), METH_NOARGS, ""},
  {NULL}  /* Sentinel */
};

static PyNumberMethods setset_as_number = {
  0,                                  /*nb_add*/
  reinterpret_cast<binaryfunc>(setset_difference), /*nb_subtract*/
  0,                                  /*nb_multiply*/
  reinterpret_cast<binaryfunc>(setset_quotient), /*nb_divide*/
  reinterpret_cast<binaryfunc>(setset_remainder), /*nb_remainder*/
  0,                                  /*nb_divmod*/
  0,                                  /*nb_power*/
  0,                                  /*nb_negative*/
  0,                                  /*nb_positive*/
  0,                                  /*nb_absolute*/
  reinterpret_cast<inquiry>(setset_nonzero), /*nb_nonzero*/
  reinterpret_cast<unaryfunc>(setset_invert), /*nb_invert*/
  0,                                  /*nb_lshift*/
  0,                                  /*nb_rshift*/
  reinterpret_cast<binaryfunc>(setset_intersection), /*nb_and*/
  reinterpret_cast<binaryfunc>(setset_symmetric_difference), /*nb_xor*/
  reinterpret_cast<binaryfunc>(setset_union), /*nb_or*/
  0/*reinterpret_cast<coercion>(Py_TPFLAGS_CHECKTYPES)*/, /*nb_coerce*/
  0,                                  /*nb_int*/
  0,                                  /*nb_long*/
  0,                                  /*nb_float*/
  0,                                  /*nb_oct*/
  0,                                  /*nb_hex*/
  0,                                  /*nb_inplace_add*/
  reinterpret_cast<binaryfunc>(setset_difference_update), /*nb_inplace_subtract*/
  0,                                  /*nb_inplace_multiply*/
  reinterpret_cast<binaryfunc>(setset_quotient_update), /*nb_inplace_divide*/
  reinterpret_cast<binaryfunc>(setset_remainder_update), /*nb_inplace_remainder*/
  0,                                  /*nb_inplace_power*/
  0,                                  /*nb_inplace_lshift*/
  0,                                  /*nb_inplace_rshift*/
  reinterpret_cast<binaryfunc>(setset_intersection_update), /*nb_inplace_and*/
  reinterpret_cast<binaryfunc>(setset_symmetric_difference_update), /*nb_inplace_xor*/
  reinterpret_cast<binaryfunc>(setset_update), /*nb_inplace_or*/
};

static PySequenceMethods setset_as_sequence = {
  setset_len,                         /* sq_length */
  0,                                  /* sq_concat */
  0,                                  /* sq_repeat */
  0,                                  /* sq_item */
  0,                                  /* sq_slice */
  0,                                  /* sq_ass_item */
  0,                                  /* sq_ass_slice */
  reinterpret_cast<objobjproc>(setset_contains), /* sq_contains */
};

PyDoc_STRVAR(setset_doc,
"Hidden class to implement graphillion classes.\n\
\n\
A setset object stores a set of sets.  A set element must be a\n\
positive number.");

PyTypeObject PySetset_Type = {
  PyObject_HEAD_INIT(NULL)
  0,                                  /*ob_size*/
  "_graphillion.setset",              /*tp_name*/
  sizeof(PySetsetObject),             /*tp_basicsize*/
  0,                                  /*tp_itemsize*/
  reinterpret_cast<destructor>(setset_dealloc), /*tp_dealloc*/
  0,                                  /*tp_print*/
  0,                                  /*tp_getattr*/
  0,                                  /*tp_setattr*/
  setset_nocmp,                       /*tp_compare*/
  reinterpret_cast<reprfunc>(setset_repr), /*tp_repr*/
  &setset_as_number,                  /*tp_as_number*/
  &setset_as_sequence,                /*tp_as_sequence*/
  0,                                  /*tp_as_mapping*/
  0,                                  /*tp_hash */
  0,                                  /*tp_call*/
  0,                                  /*tp_str*/
  0,                                  /*tp_getattro*/
  0,                                  /*tp_setattro*/
  0,                                  /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_CHECKTYPES, /*tp_flags*/
  setset_doc,                         /* tp_doc */
  0,		                      /* tp_traverse */
  0,		                      /* tp_clear */
  reinterpret_cast<richcmpfunc>(setset_richcompare), /* tp_richcompare */
  0,		                      /* tp_weaklistoffset */
  0,                                  /* tp_iter */
  0,                                  /* tp_iternext */
  setset_methods,                     /* tp_methods */
  setset_members,                     /* tp_members */
  0,                                  /* tp_getset */
  0,                                  /* tp_base */
  0,                                  /* tp_dict */
  0,                                  /* tp_descr_get */
  0,                                  /* tp_descr_set */
  0,                                  /* tp_dictoffset */
  reinterpret_cast<initproc>(setset_init), /* tp_init */
  PyType_GenericAlloc,                /* tp_alloc */
  setset_new,                         /* tp_new */
};

static PyObject* setset_elem_limit(PyObject*) {
  return PyInt_FromLong(setset::elem_limit());
}

static PyObject* setset_num_elems(PyObject*, PyObject* args) {
  PyObject* obj = NULL;
  if (!PyArg_ParseTuple(args, "|O", &obj)) return NULL;
  if (obj == NULL) {
    return PyInt_FromLong(setset::num_elems());
  } else {
    setset::num_elems(PyInt_AsLong(obj));
    Py_RETURN_NONE;
  }
}

static PyObject* graphset_subgraph(PyObject*, PyObject* args, PyObject* kwds) {
  static char s1[] = "graph";
  static char s2[] = "vertex_groups";
  static char s3[] = "degree_constraints";
  static char s4[] = "num_edges";
  static char s5[] = "num_comps";
  static char s6[] = "no_loop";
  static char* kwlist[7] = {s1, s2, s3, s4, s5, s6, NULL};
  PyObject* graph_obj = NULL;
  PyObject* vertex_groups_obj = NULL;
  PyObject* degree_constraints_obj = NULL;
  PyObject* num_edges_obj = NULL;
  int num_comps = -1, no_loop = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOOii", kwlist, &graph_obj,
                                   &vertex_groups_obj, &degree_constraints_obj,
                                   &num_edges_obj, &num_comps, &no_loop))
    return NULL;

  vector<pair<string, string> > graph;
  if (graph_obj == NULL || graph_obj == Py_None) {
    PyErr_SetString(PyExc_TypeError, "no graph");
    return NULL;
  }
  PyObject* i = PyObject_GetIter(graph_obj);
  if (i == NULL) return NULL;
  PyObject* eo;
  while ((eo = PyIter_Next(i))) {
    PyObject* j = PyObject_GetIter(eo);
    if (j == NULL) return NULL;
    vector<string> e;
    PyObject* vo;
    while ((vo = PyIter_Next(j))) {
      if (!PyString_Check(vo)) {
        PyErr_SetString(PyExc_TypeError, "invalid graph");
        return NULL;
      }
      string v = PyString_AsString(vo);
      if (v.find(',') != string::npos) {
        PyErr_SetString(PyExc_TypeError, "invalid vertex in the graph");
        return NULL;
      }
      e.push_back(v);
    }
    assert(e.size() == 2);
    graph.push_back(make_pair(e[0], e[1]));
  }

  vector<vector<string> >* vertex_groups = NULL;
  if (vertex_groups_obj != NULL && vertex_groups_obj != Py_None) {
    vertex_groups = new vector<vector<string> >();
    PyObject* i = PyObject_GetIter(vertex_groups_obj);
    if (i == NULL) return NULL;
    PyObject* uo;
    while ((uo = PyIter_Next(i))) {
      PyObject* j = PyObject_GetIter(uo);
      if (j == NULL) return NULL;
      vector<string> v;
      PyObject* vo;
      while ((vo = PyIter_Next(j))) {
        if (!PyString_Check(vo)) {
          PyErr_SetString(PyExc_TypeError, "invalid vertex groups");
          return NULL;
        }
        string vertex = PyString_AsString(vo);
        v.push_back(vertex);
      }
      vertex_groups->push_back(v);
    }
  }

  map<string, Range>* degree_constraints = NULL;
  if (degree_constraints_obj != NULL && degree_constraints_obj != Py_None) {
    degree_constraints = new map<string, Range>();
    PyObject* vo;
    PyObject* lo;
    Py_ssize_t pos = 0;
    while (PyDict_Next(degree_constraints_obj, &pos, &vo, &lo)) {
      if (!PyString_Check(vo)) {
        PyErr_SetString(PyExc_TypeError, "invalid vertex in degree constraints");
        return NULL;
      }
      string vertex = PyString_AsString(vo);
      PyObject* i = PyObject_GetIter(lo);
      if (i == NULL) return NULL;
      vector<int> r;
      PyObject* io;
      while ((io = PyIter_Next(i))) {
        if (!PyInt_Check(io)) {
          Py_DECREF(io);
          PyErr_SetString(PyExc_TypeError, "invalid degree in degree constraints");
          return NULL;
        }
        r.push_back(PyInt_AsLong(io));
      }
      (*degree_constraints)[vertex] = Range(r[0], r[1], r[2]);
    }
  }

  Range* num_edges = NULL;
  if (num_edges_obj != NULL && num_edges_obj != Py_None) {
    vector<int> r;
    PyObject* i = PyObject_GetIter(num_edges_obj);
    PyObject* io;
    while ((io = PyIter_Next(i))) {
      if (!PyInt_Check(io)) {
        Py_DECREF(io);
        PyErr_SetString(PyExc_TypeError, "invalid number of edges");
        return NULL;
      }
      r.push_back(PyInt_AsLong(io));
    }
    num_edges = new Range(r[0], r[1], r[2]);
  }

  setset ss = FrontierSearch(graph, vertex_groups, degree_constraints, num_edges,
                             num_comps, no_loop);

  PySetsetObject* ret = reinterpret_cast<PySetsetObject*>
      (PySetset_Type.tp_alloc(&PySetset_Type, 0));
  ret->ss = new setset(ss);
  return reinterpret_cast<PyObject*>(ret);
}

static PyMethodDef module_methods[] = {
  {"_elem_limit", reinterpret_cast<PyCFunction>(setset_elem_limit), METH_NOARGS, ""},
  {"_num_elems", setset_num_elems, METH_VARARGS, ""},
  {"_subgraph", reinterpret_cast<PyCFunction>(graphset_subgraph), METH_VARARGS | METH_KEYWORDS, ""},
  {NULL}  /* Sentinel */
};

PyDoc_STRVAR(graphillion_doc,
"Hidden module to implement graphillion classes.");

#ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC init_graphillion(void) {
  PyObject* m;
  if (PyType_Ready(&PySetset_Type) < 0) return;
  if (PyType_Ready(&PySetsetIter_Type) < 0) return;
  m = Py_InitModule3("_graphillion", module_methods, graphillion_doc);
  if (m == NULL) return;
  Py_INCREF(&PySetset_Type);
  Py_INCREF(&PySetsetIter_Type);
  PyModule_AddObject(m, "setset", reinterpret_cast<PyObject*>(&PySetset_Type));
  PyModule_AddObject(m, "setset_iterator",
                     reinterpret_cast<PyObject*>(&PySetsetIter_Type));
}