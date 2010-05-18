#ifndef __POINTLESS__MODULE__H__
#define __POINTLESS__MODULE__H__

#include <Python.h>
#include <Judy.h>

#include "structmember.h"

#include <stdio.h>
#include <limits.h>

#include <pointless/pointless.h>
#include <pointless/pointless_dynarray.h>

#define POINTLESS_FUNC_N_DOC(func) PyObject* func(PyObject* o, PyObject* args); extern const char func##_doc[]
#define POINTLESS_FUNC_DEF(pyfunc, func) { pyfunc, func, METH_VARARGS, func##_doc}

POINTLESS_FUNC_N_DOC(pointless_write_object);
POINTLESS_FUNC_N_DOC(pointless_pyobject_hash);
POINTLESS_FUNC_N_DOC(pointless_cmp);
POINTLESS_FUNC_N_DOC(pointless_is_eq);
POINTLESS_FUNC_N_DOC(pointless_db_array_sort);

typedef struct {
	PyObject_HEAD
	int is_open;
	pointless_t p;
} PyPointless;

typedef struct {
	PyObject_HEAD
	PyPointless* pp;
	pointless_value_t* v;
	unsigned long container_id;
	int is_hashable;

	// slice params, must be respected at all times
	uint32_t slice_i;
	uint32_t slice_n;
} PyPointlessVector;

typedef struct {
	PyObject_HEAD
	PyPointlessVector* vector;
	uint32_t iter_state;
} PyPointlessVectorIter;

typedef struct {
	PyObject_HEAD
	int is_pointless;

	// pointless stuff
	PyPointless* pointless_pp;
	pointless_value_t* pointless_v;

	// other stuff
	uint32_t primitive_n_bits;
	void* primitive_bits;
	uint32_t primitive_n_bytes_alloc;
} PyPointlessBitvector;

typedef struct {
	PyObject_HEAD
	PyPointlessBitvector* bitvector;
	uint32_t iter_state;
} PyPointlessBitvectorIter;

typedef struct {
	PyObject_HEAD
	PyPointless* pp;
	pointless_value_t* v;
	unsigned long container_id;
} PyPointlessSet;

typedef struct {
	PyObject_HEAD
	PyPointlessSet* set;
	uint32_t iter_state;
} PyPointlessSetIter;

typedef struct {
	PyObject_HEAD
	PyPointless* pp;
	pointless_value_t* v;
	unsigned long container_id;
} PyPointlessMap;

typedef struct {
	PyObject_HEAD
	PyPointlessMap* map;
	uint32_t iter_state;
} PyPointlessMapKeyIter;

typedef struct {
	PyObject_HEAD
	PyPointlessMap* map;
	uint32_t iter_state;
} PyPointlessMapValueIter;

typedef struct {
	PyObject_HEAD
	PyPointlessMap* map;
	uint32_t iter_state;
} PyPointlessMapItemIter;

#define POINTLESS_PRIM_VECTOR_TYPE_I8 0
#define POINTLESS_PRIM_VECTOR_TYPE_U8 1
#define POINTLESS_PRIM_VECTOR_TYPE_I16 2
#define POINTLESS_PRIM_VECTOR_TYPE_U16 3
#define POINTLESS_PRIM_VECTOR_TYPE_I32 4
#define POINTLESS_PRIM_VECTOR_TYPE_U32 5
#define POINTLESS_PRIM_VECTOR_TYPE_FLOAT 6

typedef struct {
	PyObject_HEAD
	pointless_dynarray_t array;
	uint8_t type;
} PyPointlessPrimVector;

typedef struct {
	PyObject_HEAD
	PyPointlessPrimVector* vector;
	uint32_t iter_state;
} PyPointlessPrimVectorIter;


PyPointlessPrimVector* PyPointlessPrimVector_from_T_vector(pointless_dynarray_t* v, uint32_t t);

PyPointlessVector* PyPointlessVector_New(PyPointless* pp, pointless_value_t* v, uint32_t slice_i, uint32_t slice_n);
PyPointlessBitvector* PyPointlessBitvector_New(PyPointless* pp, pointless_value_t* v);

PyPointlessSet* PyPointlessSet_New(PyPointless* pp, pointless_value_t* v);
PyPointlessMap* PyPointlessMap_New(PyPointless* pp, pointless_value_t* v);

PyObject* pypointless_i32(PyPointless* p, int32_t v);
PyObject* pypointless_u32(PyPointless* p, uint32_t v);
PyObject* pypointless_float(PyPointless* p, float v);
PyObject* pypointless_value_unicode(pointless_t* p, pointless_value_t* v);
PyObject* pypointless_value(PyPointless* p, pointless_value_t* v);

PyObject* PyPointless_str(PyObject* py_object);
PyObject* PyPointless_repr(PyObject* py_object);

uint32_t pypointless_cmp_eq(pointless_t* p, pointless_value_t* v, PyObject* py_object, const char** error);
uint32_t pyobject_hash(PyObject* py_object, const char** error);
uint32_t pointless_pybitvector_hash(PyPointlessBitvector* bitvector);

// custom types
extern PyTypeObject PyPointlessType;
extern PyTypeObject PyPointlessVectorType;
extern PyTypeObject PyPointlessVectorIterType;
extern PyTypeObject PyPointlessBitvectorType;
extern PyTypeObject PyPointlessBitvectorIterType;
extern PyTypeObject PyPointlessSetType;
extern PyTypeObject PyPointlessSetIterType;
extern PyTypeObject PyPointlessMapType;
extern PyTypeObject PyPointlessMapKeyIterType;
extern PyTypeObject PyPointlessMapValueIterType;
extern PyTypeObject PyPointlessMapItemIterType;
extern PyTypeObject PyPointlessPrimVectorType;
extern PyTypeObject PyPointlessPrimVectorIterType;

#define PyPointless_Check(op) PyObject_TypeCheck(op, &PyPointlessType)
#define PyPointlessVector_Check(op) PyObject_TypeCheck(op, &PyPointlessVectorType)
#define PyPointlessBitvector_Check(op) PyObject_TypeCheck(op, &PyPointlessBitvectorType)
#define PyPointlessSet_Check(op) PyObject_TypeCheck(op, &PyPointlessSetType)
#define PyPointlessMap_Check(op) PyObject_TypeCheck(op, &PyPointlessMapType)
#define PyPointlessPrimVector_Check(op) PyObject_TypeCheck(op, &PyPointlessPrimVectorType)

// C-API
PyPointlessPrimVector* PyPointlessPrimVector_from_T_vector(pointless_dynarray_t* v, uint32_t t);

typedef struct {
	// prim-vector operations
	void(*primvector_init)(pointless_dynarray_t* a, size_t item_size);
	size_t(*primvector_n_items)(pointless_dynarray_t* a);
	void(*primvector_pop)(pointless_dynarray_t* a);
	int(*primvector_push)(pointless_dynarray_t* a, void* i);
	void(*primvector_clear)(pointless_dynarray_t* a);
	void(*primvector_destroy)(pointless_dynarray_t* a);

	// prim-vector object constructors
	PyPointlessPrimVector*(*primvector_from_vector)(pointless_dynarray_t* v, uint32_t t);
} PyPointless_CAPI;

#define POINTLESS_API_MAGIC 0xB6D89E08

static PyPointless_CAPI* PyPointlessAPI;

#define PyPointless_IMPORT \
	(PyPointlessAPI = (PyPointless_CAPI*)PyCObject_Import("pointless", "pointless_CAPI"))

#endif
