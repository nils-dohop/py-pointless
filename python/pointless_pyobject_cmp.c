#include "../pointless.h"

// a container for either a pointless value, or PyObject*
typedef struct {
	int32_t is_pointless;

	union {
		struct {
			pointless_t* p;
			pointless_value_t v; // we do not have a ptr, because we want inline values for arbitrary vector items
			uint32_t vector_slice_i;
			uint32_t vector_slice_n;
		} pointless;

		PyObject* py_object;
	} value;
} pypointless_cmp_value_t;

// cmp state
typedef struct {
	const char* error;
	uint32_t depth;
} pypointless_cmp_state_t;

// number values
typedef struct {
	int32_t is_int;
	int64_t i;
	float f;
} pypointless_cmp_int_float_bool_t;

static void pypointless_cmp_value_init_pointless(pypointless_cmp_value_t* cv, pointless_t* p, pointless_value_t* v)
{
	cv->is_pointless = 1;
	cv->value.pointless.p = p;
	cv->value.pointless.v = *v;
	cv->value.pointless.vector_slice_i = 0;
	cv->value.pointless.vector_slice_n = 0;

	if (pointless_is_vector_type(v->type)) {
		cv->value.pointless.vector_slice_i = 0;
		cv->value.pointless.vector_slice_n = pointless_reader_vector_n_items(p, v);
	}
}

static void pypointless_cmp_value_init_python(pypointless_cmp_value_t* v, PyObject* py_object)
{
	v->value.pointless.vector_slice_i = 0;
	v->value.pointless.vector_slice_n = 0;

	if (PyPointlessVector_Check(py_object)) {
		v->is_pointless = 1;
		v->value.pointless.p = &(((PyPointlessVector*)py_object)->pp->p);
		v->value.pointless.v = *(((PyPointlessVector*)py_object)->v);
		v->value.pointless.vector_slice_i = ((PyPointlessVector*)py_object)->slice_i;
		v->value.pointless.vector_slice_n = ((PyPointlessVector*)py_object)->slice_n;
	} else if (PyPointlessBitvector_Check(py_object) && ((PyPointlessBitvector*)py_object)->is_pointless) {
		v->is_pointless = 1;
		v->value.pointless.p = &(((PyPointlessBitvector*)py_object)->pointless_pp->p);
		v->value.pointless.v = *(((PyPointlessBitvector*)py_object)->pointless_v);
	} else if (PyPointlessSet_Check(py_object)) {
		v->is_pointless = 1;
		v->value.pointless.p = &(((PyPointlessSet*)py_object)->pp->p);
		v->value.pointless.v = *(((PyPointlessSet*)py_object)->v);
	} else if (PyPointlessMap_Check(py_object)) {
		v->is_pointless = 1;
		v->value.pointless.p = &(((PyPointlessMap*)py_object)->pp->p);
		v->value.pointless.v = *(((PyPointlessMap*)py_object)->v);
	} else {
		v->is_pointless = 0;
		v->value.py_object = py_object;
	}
}

static int32_t pypointless_is_pylong_negative(PyObject* py_object, pypointless_cmp_state_t* state)
{
	PyObject* i = PyInt_FromLong(0);
	int32_t retval = 0;

	if (i == 0) {
		PyErr_Clear();
		state->error = "out of memory";
		return retval;
	}

	int c = PyObject_RichCompareBool(py_object, i, Py_LT);

	if (c == -1) {
		PyErr_Clear();
		state->error = "integer rich-compare error";
		return retval;
	}

	if (c == 1)
		retval = 1;

	Py_DECREF(i);

	return retval;
}

// most basic cmp function
static int32_t pypointless_cmp_rec(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state);

// forward declerations of all type-specific comparison functions
typedef int32_t (*pypointless_cmp_cb)(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state);

static int32_t pypointless_cmp_unicode(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state);
static int32_t pypointless_cmp_int_float_bool(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state);
static int32_t pypointless_cmp_none(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state);
static int32_t pypointless_cmp_vector(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state);
static int32_t pypointless_cmp_bitvector(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state);

// mapping of pointless value/PyObject* to a type specific comparison function
static pypointless_cmp_cb pypointless_cmp_func(pypointless_cmp_value_t* v, uint32_t* type, pypointless_cmp_state_t* state)
{
	if (v->is_pointless) {
		*type = v->value.pointless.v.type;

		switch (*type) {
			case POINTLESS_I32:
			case POINTLESS_U32:
			case POINTLESS_FLOAT:
			case POINTLESS_BOOLEAN:
				return pypointless_cmp_int_float_bool;
			case POINTLESS_NULL:
				return pypointless_cmp_none;
			case POINTLESS_UNICODE:
				return pypointless_cmp_unicode;
			case POINTLESS_SET_VALUE:
			case POINTLESS_MAP_VALUE_VALUE:
			case POINTLESS_EMPTY_SLOT:
				return 0;
		}

		if (pointless_is_vector_type(*type))
			return pypointless_cmp_vector;

		if (pointless_is_bitvector_type(*type))
			return pypointless_cmp_bitvector;

		// some illegal type, anyways, we cannot compare it
		state->error = "comparison not supported for pointless type";
		return 0;
	} else {
		// we need to check for every useful Python type
		PyObject* py_object = v->value.py_object;

		if (PyInt_Check(py_object)) {
			if (PyInt_AS_LONG(py_object) < 0)
				*type = POINTLESS_I32;
			else
				*type = POINTLESS_U32;

			return pypointless_cmp_int_float_bool;
		}

		if (PyLong_Check(py_object)) {
			if (pypointless_is_pylong_negative(py_object, state))
				*type = POINTLESS_I32;
			else
				*type = POINTLESS_U32;

			return pypointless_cmp_int_float_bool;
		}

		if (PyFloat_Check(py_object)) {
			*type = POINTLESS_FLOAT;
			return pypointless_cmp_int_float_bool;
		}

		if (PyBool_Check(py_object)) {
			*type = POINTLESS_BOOLEAN;
			return pypointless_cmp_int_float_bool;
		}

		if (py_object == Py_None) {
			*type = POINTLESS_NULL;
			return pypointless_cmp_none;
		}

		if (PyString_Check(py_object) || PyUnicode_Check(py_object)) {
			*type = POINTLESS_UNICODE;
			return pypointless_cmp_unicode;
		}

		if (PyAnySet_Check(py_object)) {
			*type = POINTLESS_SET_VALUE;
			return 0;
		}

		if (PyDict_Check(py_object)) {
			*type = POINTLESS_MAP_VALUE_VALUE;
			return 0;
		}

		if (PyList_Check(py_object)) {
			*type = POINTLESS_VECTOR_VALUE;
			return pypointless_cmp_vector;
		}

		if (PyTuple_Check(py_object)) {
			*type = POINTLESS_VECTOR_VALUE;
			return pypointless_cmp_vector;
		}

		if (PyPointlessBitvector_Check(py_object)) {
			*type = POINTLESS_BITVECTOR;
			return pypointless_cmp_bitvector;
		}

		// no known type
		state->error = "comparison not supported for Python type";
		*type = UINT32_MAX;
		return 0;
	}
}

static void pypointless_cmp_extract_unicode_or_string(pypointless_cmp_value_t* v, uint32_t** unicode, uint8_t** string, pypointless_cmp_state_t* state)
{
#ifndef Py_UNICODE_WIDE
	state->error = "we only support unicode comparison on UCS-4 builds";
	return;
#else
	if (v->is_pointless) {
		*unicode = pointless_reader_unicode_value_ucs4(v->value.pointless.p, &v->value.pointless.v);
		return;
	}

	assert(PyString_Check(v->value.py_object) || PyUnicode_Check(v->value.py_object));

	if (PyString_Check(v->value.py_object))
		*string = (uint8_t*)PyString_AS_STRING(v->value.py_object);
	else
		*unicode = (uint32_t*)PyUnicode_AS_UNICODE(v->value.py_object);
#endif
}

static int32_t pypointless_cmp_unicode(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state)
{
	uint32_t* unicode_a = 0;
	uint8_t* string_a = 0;

	uint32_t* unicode_b = 0;
	uint8_t* string_b = 0;

	pypointless_cmp_extract_unicode_or_string(a, &unicode_a, &string_a, state);

	if (state->error)
		return 0;

	pypointless_cmp_extract_unicode_or_string(b, &unicode_b, &string_b, state);

	if (state->error)
		return 0;

	if (unicode_a && unicode_b)
		return pointless_cmp_unicode_ucs4_ucs4(unicode_a, unicode_b);

	if (unicode_a && string_b)
		return pointless_cmp_unicode_ucs4_ascii(unicode_a, string_b);

	if (string_a && unicode_b)
		return pointless_cmp_unicode_ascii_ucs4(string_a, unicode_b);

	assert(string_a && string_b);

	return pointless_cmp_unicode_ascii_ascii(string_a, string_b);
}

static pypointless_cmp_int_float_bool_t pypointless_cmp_int_float_bool_from_value(pypointless_cmp_value_t* v, pypointless_cmp_state_t* state)
{
	pypointless_cmp_int_float_bool_t r;
	r.is_int = 1;
	r.i = 0;
	r.f = 0.0f;

	if (v->is_pointless) {
		pointless_value_t* pv = &v->value.pointless.v;

		switch (pv->type) {
			case POINTLESS_I32:
				r.is_int = 1;
				r.i = (int64_t)pointless_value_get_i32(pv->type, &pv->data);
				return r;
			case POINTLESS_U32:
				r.is_int = 1;
				r.i = (int64_t)pointless_value_get_u32(pv->type, &pv->data);
				return r;
			case POINTLESS_FLOAT:
				r.is_int = 0;
				r.f = pointless_value_get_float(pv->type, &pv->data);
				return r;
			case POINTLESS_BOOLEAN:
				r.is_int = 1;
				r.i = (int64_t)pointless_value_get_bool(pv->type, &pv->data);
				return r;
		}
	} else {
		PyObject* py_object = v->value.py_object;

		if (PyInt_Check(py_object)) {
			r.is_int = 1;
			r.i = (int64_t)PyInt_AS_LONG(py_object);
			return r;
		} else if (PyLong_Check(py_object)) {
			PY_LONG_LONG v = PyLong_AsLongLong(py_object);

			if (PyErr_Occurred()) {
				PyErr_Clear();
				state->error = "python long too big for comparison";
				return r;
			}

			if (!(INT64_MIN <= v && INT64_MAX <= v)) {
				state->error = "python long too big for comparison";
				return r;
			}

			r.is_int = 1;
			r.i = (int64_t)v;
			return r;
		} else if (PyFloat_Check(py_object)) {
			r.is_int = 0;
			r.f = (float)PyFloat_AS_DOUBLE(py_object);
			return r;
		} else if (PyBool_Check(py_object)) {
			r.is_int = 0;

			if (py_object == Py_True)
				r.is_int = 1;
			else
				r.is_int = 0;

			return r;
		}
	}

	state->error = "int/float/bool comparison internal error";
	return r;
}

static int32_t pypointless_cmp_int_float_bool_priv(pypointless_cmp_int_float_bool_t* v_a, pypointless_cmp_int_float_bool_t* v_b)
{
	if (v_a->is_int && v_b->is_int)
		return SIMPLE_CMP(v_a->i, v_b->i);

	if (!v_a->is_int && v_b->is_int)
		return SIMPLE_CMP(v_a->f, v_b->i);

	if (v_a->is_int && !v_b->is_int)
		return SIMPLE_CMP(v_a->i, v_b->f);

	assert(!v_a->is_int && !v_b->is_int);

	return SIMPLE_CMP(v_a->f, v_b->f);
}

static int32_t pypointless_cmp_int_float_bool(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state)
{
	pypointless_cmp_int_float_bool_t v_a = pypointless_cmp_int_float_bool_from_value(a, state);

	if (state->error)
		return 0;

	pypointless_cmp_int_float_bool_t v_b = pypointless_cmp_int_float_bool_from_value(b, state);

	if (state->error)
		return 0;

	return pypointless_cmp_int_float_bool_priv(&v_a, &v_b);
}

static int32_t pypointless_cmp_none(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state)
{
	// always equal
	return 0;
}

static uint32_t pypointless_cmp_vector_n_items(pypointless_cmp_value_t* a)
{
	if (a->is_pointless)
		return a->value.pointless.vector_slice_n;

	assert(PyList_Check(a->value.py_object) || PyTuple_Check(a->value.py_object));

	if (PyList_Check(a->value.py_object))
		return (uint32_t)PyList_GET_SIZE(a->value.py_object);

	return (uint32_t)PyTuple_GET_SIZE(a->value.py_object);
}

static pypointless_cmp_value_t pypointless_cmp_vector_item_at(pypointless_cmp_value_t* v, uint32_t i)
{
	// our return value
	pypointless_cmp_value_t r;

	// pointless
	if (v->is_pointless) {
		r.is_pointless = 1;
		r.value.pointless.p = v->value.pointless.p;
		r.value.pointless.v = pointless_reader_vector_value_case(v->value.pointless.p, &v->value.pointless.v, i + v->value.pointless.vector_slice_i);
		r.value.pointless.vector_slice_i = 0;
		r.value.pointless.vector_slice_n = 0;

		if (pointless_is_vector_type(r.value.pointless.v.type)) {
			r.value.pointless.vector_slice_i = 0;
			r.value.pointless.vector_slice_n = pointless_reader_vector_n_items(v->value.pointless.p, &r.value.pointless.v);
		}
	// PyObject
	} else {
		r.is_pointless = 0;

		assert(PyList_Check(v->value.py_object) || PyTuple_Check(v->value.py_object));

		if (PyList_Check(v->value.py_object))
			r.value.py_object = PyList_GET_ITEM(v->value.py_object, i);
		else
			r.value.py_object = PyTuple_GET_ITEM(v->value.py_object, i);
	}

	return r;
}

static int32_t pypointless_cmp_vector(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state)
{
	uint32_t n_items_a = pypointless_cmp_vector_n_items(a);
	uint32_t n_items_b = pypointless_cmp_vector_n_items(b);
	uint32_t i, n_items = (n_items_a < n_items_b) ? n_items_a : n_items_b;
	int32_t c;

	pypointless_cmp_value_t v_a, v_b;

	for (i = 0; i < n_items; i++) {
		v_a = pypointless_cmp_vector_item_at(a, i);
		v_b = pypointless_cmp_vector_item_at(b, i);
		c = pypointless_cmp_rec(&v_a, &v_b, state);

		if (c != 0)
			return c;
	}

	return SIMPLE_CMP(n_items_a, n_items_b);
}

static uint32_t pypointless_cmp_bitvector_n_items(pypointless_cmp_value_t* v)
{
	if (v->is_pointless)
		return pointless_reader_bitvector_n_bits(v->value.pointless.p, &v->value.pointless.v);

	assert(PyPointlessBitvector_Check(v->value.py_object));

	PyPointlessBitvector* bv = (PyPointlessBitvector*)v->value.py_object;

	if (bv->is_pointless)
		return pointless_reader_bitvector_n_bits(&bv->pointless_pp->p, bv->pointless_v);

	return bv->primitive_n_bits;
}

static uint32_t pypointless_cmp_bitvector_item_at(pypointless_cmp_value_t* v, uint32_t i)
{
	if (v->is_pointless)
		return pointless_reader_bitvector_is_set(v->value.pointless.p, &v->value.pointless.v, i);

	assert(PyPointlessBitvector_Check(v->value.py_object));

	PyPointlessBitvector* bv = (PyPointlessBitvector*)v->value.py_object;

	if (bv->is_pointless)
		return pointless_reader_bitvector_is_set(&bv->pointless_pp->p, bv->pointless_v, i);

	return (bm_is_set_(bv->primitive_bits, i) != 0);
}

static int32_t pypointless_cmp_bitvector(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state)
{
	uint32_t n_items_a = pypointless_cmp_bitvector_n_items(a);
	uint32_t n_items_b = pypointless_cmp_bitvector_n_items(b);
	uint32_t i, n_items = (n_items_a < n_items_b) ? n_items_a : n_items_b;
	uint32_t v_a, v_b;
	int32_t c;

	for (i = 0; i < n_items; i++) {
		v_a = pypointless_cmp_bitvector_item_at(a, i);
		v_b = pypointless_cmp_bitvector_item_at(b, i);

		c = SIMPLE_CMP(v_a, v_b);

		if (c != 0)
			return c;
	}

	return SIMPLE_CMP(n_items_a, n_items_b);
}

static int32_t pypointless_cmp_rec(pypointless_cmp_value_t* a, pypointless_cmp_value_t* b, pypointless_cmp_state_t* state)
{
	// check for too much depth
	if (state->depth >= POINTLESS_MAX_DEPTH) {
		state->error = "maximum recursion depth reached during comparison";
		return 0;
	}

	// get the two comparison functions needed
	uint32_t t_a, t_b;
	pypointless_cmp_cb cmp_a = pypointless_cmp_func(a, &t_a, state);
	pypointless_cmp_cb cmp_b = pypointless_cmp_func(b, &t_b, state);
	int32_t c;

	// we're going one deep
	state->depth += 1;

	if (cmp_a == 0 || cmp_b == 0 || cmp_a != cmp_b)
		c = SIMPLE_CMP(t_a, t_b);
	else
		c = (*cmp_a)(a, b, state);

	// ...and up again
	state->depth -= 1;

	// return cmp result
	return c;
}

uint32_t pypointless_cmp_eq(pointless_t* p, pointless_value_t* v, PyObject* py_object, const char** error)
{
	pypointless_cmp_value_t v_a, v_b;
	int32_t c;
	pypointless_cmp_state_t state;

	pypointless_cmp_value_init_pointless(&v_a, p, v);
	pypointless_cmp_value_init_python(&v_b, py_object);

	state.error = 0;
	state.depth = 0;

	c = pypointless_cmp_rec(&v_a, &v_b, &state);

	if (state.error != 0) {
		*error = state.error;
		return 0;
	}

	return (c == 0);
}

const char pointless_cmp_doc[] =
"2\n"
"pointless.pointless_cmp(a, b)\n"
"\n"
"Return a pointless-consistent comparison of two objects.\n"
"\n"
"  a: first object\n"
"  b: second object\n"
;
PyObject* pointless_cmp(PyObject* self, PyObject* args)
{
	PyObject* a = 0;
	PyObject* b = 0;
	pypointless_cmp_value_t v_a, v_b;
	int32_t c;
	pypointless_cmp_state_t state;

	if (!PyArg_ParseTuple(args, "OO:pointless_cmp", &a, &b))
		return 0;

	pypointless_cmp_value_init_python(&v_a, a);
	pypointless_cmp_value_init_python(&v_b, b);

	state.error = 0;
	state.depth = 0;

	c = pypointless_cmp_rec(&v_a, &v_b, &state);

	if (state.error) {
		PyErr_Format(PyExc_ValueError, "pointless_cmp: %s", state.error);
		return 0;
	}

	return PyInt_FromLong((long)c);
}

const char pointless_is_eq_doc[] =
"3\n"
"pointless.pointless_is_eq(a, b)\n"
"\n"
"Return true iff: a == b, as defined by pointless.\n"
"\n"
"  a: first object\n"
"  b: second object\n"
;
PyObject* pointless_is_eq(PyObject* self, PyObject* args)
{
	PyObject* a = 0;
	PyObject* b = 0;
	pypointless_cmp_value_t v_a, v_b;
	int32_t c;
	pypointless_cmp_state_t state;

	if (!PyArg_ParseTuple(args, "OO:pointless_is_eq", &a, &b))
		return 0;

	pypointless_cmp_value_init_python(&v_a, a);
	pypointless_cmp_value_init_python(&v_b, b);

	state.error = 0;
	state.depth = 0;

	c = pypointless_cmp_rec(&v_a, &v_b, &state);

	if (state.error) {
		PyErr_Format(PyExc_ValueError, "pointless_cmp: %s", state.error);
		return 0;
	}

	if (c == 0)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}
