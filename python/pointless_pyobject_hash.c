#include "../pointless_ext.h"

typedef struct {
	uint32_t version;
	uint32_t depth;
	const char** error;
} pyobject_hash_state_t;

static uint32_t pyobject_hash_rec(PyObject* py_object, pyobject_hash_state_t* state);

static uint32_t pyobject_hash_tuple(PyObject* py_object, pyobject_hash_state_t* state)
{
	Py_ssize_t i, s = PyTuple_GET_SIZE(py_object);

	if (s > UINT32_MAX) {
		*state->error = "tuple length is too large";
		return 0;
	}

	uint32_t h, n_items = (uint32_t)s;
	PyObject* o = 0;

	pointless_vector_hash_state_t v_state;
	pointless_vector_hash_init(&v_state, n_items);

	state->depth += 1;

	for (i = 0; i < s; i++) {
		o = PyTuple_GET_ITEM(py_object, i);
		h = pyobject_hash_rec(o, state);
		pointless_vector_hash_next(&v_state, h);
	}

	state->depth -= 1;

	return pointless_vector_hash_end(&v_state);
}

static uint32_t pyobject_hash_primvector(PyPointlessPrimVector* v, pyobject_hash_state_t* state)
{
	uint64_t i;
	size_t n_items = pointless_dynarray_n_items(&v->array);
	uint32_t h;

	pointless_vector_hash_state_t v_state;
	pointless_vector_hash_init(&v_state, (uint32_t)n_items);

	for (i = 0; i < n_items; i++) {
		void* item = pointless_dynarray_item_at(&v->array, i);

		switch (v->type) {
			case _POINTLESS_PRIM_VECTOR_TYPE_I8:
				h = pointless_hash_i32(*((int8_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_U8:
				h = pointless_hash_u32(*((uint8_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_I16:
				h = pointless_hash_i32(*((int16_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_U16:
				h = pointless_hash_u32(*((uint16_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_I32:
				h = pointless_hash_i32(*((int32_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_U32:
				h = pointless_hash_u32(*((uint32_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_I64:
				h = pointless_hash_i64(*((int64_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_U64:
				h = pointless_hash_u64(*((uint64_t*)item));
				break;
			case _POINTLESS_PRIM_VECTOR_TYPE_FLOAT:
				h = pointless_hash_float(*((float*)item));
				break;
			default:
				*state->error = "internal error";
				return 0;
		}

		pointless_vector_hash_next(&v_state, h);
	}

	return pointless_vector_hash_end(&v_state);
}

static uint32_t pyobject_hash_unicode(PyObject* py_object, pyobject_hash_state_t* state)
{
	Py_UNICODE* s = PyUnicode_AS_UNICODE(py_object);
	uint32_t hash = 0;

	switch (state->version) {
		#ifdef Py_UNICODE_WIDE
		case POINTLESS_FF_VERSION_OFFSET_32_OLDHASH:
			hash = pointless_hash_unicode_ucs4_v0((uint32_t*)s);
			break;
		case POINTLESS_FF_VERSION_OFFSET_32_NEWHASH:
		case POINTLESS_FF_VERSION_OFFSET_64_NEWHASH:
			hash = pointless_hash_unicode_ucs4_v1((uint32_t*)s);
			break;
		#else
		case POINTLESS_FF_VERSION_OFFSET_32_OLDHASH:
			hash = pointless_hash_unicode_ucs2_v0((uint16_t*)s);
			break;
		case POINTLESS_FF_VERSION_OFFSET_32_NEWHASH:
		case POINTLESS_FF_VERSION_OFFSET_64_NEWHASH:
			hash = pointless_hash_unicode_ucs2_v1((uint16_t*)s);
			break;
		#endif
	}

	return hash;
}

static uint32_t pyobject_hash_string(PyObject* py_object, pyobject_hash_state_t* state)
{
	char* s = PyString_AS_STRING(py_object);
	uint32_t hash = 0;

	switch (state->version) {
		case POINTLESS_FF_VERSION_OFFSET_32_OLDHASH:
			hash = pointless_hash_string_v0((uint8_t*)s);
			break;
		case POINTLESS_FF_VERSION_OFFSET_32_NEWHASH:
		case POINTLESS_FF_VERSION_OFFSET_64_NEWHASH:
			hash = pointless_hash_string_v1((uint8_t*)s);
			break;
	}

	return hash;
}

static uint32_t pyobject_hash_pypointlessbitvector(PyObject* py_object, pyobject_hash_state_t* state)
{
	return pointless_pybitvector_hash((PyPointlessBitvector*)py_object);
}

static uint32_t pyobject_hash_int(PyObject* py_object, pyobject_hash_state_t* state)
{
	long i = PyInt_AS_LONG(py_object);

	if (i < 0) {
		if (!(INT32_MIN <= i && i <= INT32_MAX)) {
			*state->error = "hashing of integers exceeding 32-bits not supported";
			return 0;
		}

		return pointless_hash_i32((int32_t)i);
	}

	if (!(i <= UINT32_MAX)) {
		*state->error = "hashing of integers exceeding 32-bits not supported";
		return 0;
	}

	return pointless_hash_u32((uint32_t)i);
}

static uint32_t pyobject_hash_long(PyObject* py_object, pyobject_hash_state_t* state)
{
	PY_LONG_LONG i = PyLong_AsLongLong(py_object);

	if (PyErr_Occurred()) {
		PyErr_Clear();
		*state->error = "hashing of integers exceeding 32-bits not supported";
		return 0;
	}

	if (i < 0) {
		if (!(INT32_MIN <= i && i <= INT32_MAX)) {
			*state->error = "hashing of integers exceeding 32-bits not supported";
			return 0;
		}

		return pointless_hash_i32((int32_t)i);
	}

	if (!(i <= UINT32_MAX)) {
		*state->error = "hashing of integers exceeding 32-bits not supported";
		return 0;
	}

	return pointless_hash_u32((uint32_t)i);
}

static uint32_t pyobject_hash_float(PyObject* py_object, pyobject_hash_state_t* state)
{
	float v = (float)PyFloat_AS_DOUBLE(py_object);
	return pointless_hash_float(v);
}

static uint32_t pyobject_hash_boolean(PyObject* py_object, pyobject_hash_state_t* state)
{
	if (py_object == Py_True)
		return pointless_hash_bool_true();
	else
		return pointless_hash_bool_false();
}

static uint32_t pyobject_hash_null(PyObject* py_object, pyobject_hash_state_t* state)
{
	return pointless_hash_null();
}

static uint32_t pyobject_hash_rec(PyObject* py_object, pyobject_hash_state_t* state)
{
	if (state->depth >= POINTLESS_MAX_DEPTH) {
		*state->error = "maximum depth reached";
		return 0;
	}

	// pointless types
	pointless_t* p = 0;
	pointless_value_t* v = 0;

	if (PyPointlessVector_Check(py_object)) {
		p = &((PyPointlessVector*)py_object)->pp->p;
		v = ((PyPointlessVector*)py_object)->v;

		if (!pointless_is_hashable(v->type)) {
			*state->error = "pointless type is not hashable";
			return 0;
		}

		return pointless_hash_reader_vector(p, v, ((PyPointlessVector*)py_object)->slice_i, ((PyPointlessVector*)py_object)->slice_n);
	}

	if (PyPointlessBitvector_Check(py_object))
		return pointless_pybitvector_hash((PyPointlessBitvector*)py_object);

	if (PyPointlessSet_Check(py_object)) {
		p = &((PyPointlessSet*)py_object)->pp->p;
		v = ((PyPointlessSet*)py_object)->v;

		if (!pointless_is_hashable(v->type)) {
			*state->error = "pointless type is not hashable";
			return 0;
		}

		return pointless_hash_reader(p, v);
	}

	// prim-vector
	if (PyPointlessPrimVector_Check(py_object))
		return pyobject_hash_primvector((PyPointlessPrimVector*)py_object, state);

	// early checks for more common types
	if (PyInt_Check(py_object))
		return pyobject_hash_int(py_object, state);

	if (PyLong_Check(py_object))
		return pyobject_hash_long(py_object, state);

	if (PyTuple_Check(py_object))
		return pyobject_hash_tuple(py_object, state);

	if (PyUnicode_Check(py_object))
		return pyobject_hash_unicode(py_object, state);

	if (PyString_Check(py_object))
		return pyobject_hash_string(py_object, state);

	if (PyPointlessBitvector_Check(py_object))
		return pyobject_hash_pypointlessbitvector(py_object, state);

	if (PyFloat_Check(py_object))
		return pyobject_hash_float(py_object, state);

	if (PyBool_Check(py_object))
		return pyobject_hash_boolean(py_object, state);

	if (py_object == Py_None)
		return pyobject_hash_null(py_object, state);

	*state->error = "object is not hashable";
	return 0;
}

uint32_t pyobject_hash(PyObject* py_object, uint32_t version, const char** error)
{
	pyobject_hash_state_t state;
	state.version = version;
	state.depth = 0;
	state.error = error;
	return pyobject_hash_rec(py_object, &state);
}

const char pointless_pyobject_hash_doc[] =
"1\n"
"pointless.pyobject_hash(object)\n"
"\n"
"Return a pointless-consistent hash of a Python object.\n"
"\n"
"  object: the object\n"
;
PyObject* pointless_pyobject_hash(PyObject* self, PyObject* args)
{
	PyObject* object = 0;
	const char* error = 0;
	int version = POINTLESS_FILE_FORMAT_LATEST_VERSION_;

	if (!PyArg_ParseTuple(args, "O|i:pyobject_hash", &object, &version))
		return 0;

	if (!(POINTLESS_FILE_FORMAT_OLDEST_VERSION_ <= version && version <= POINTLESS_FILE_FORMAT_LATEST_VERSION_)) {
		PyErr_Format(PyExc_ValueError, "unsupported version");
		return 0;
	}

	uint32_t hash = pyobject_hash(object, version, &error);

	if (error) {
		PyErr_Format(PyExc_ValueError, "PyObject hashing error: %s", error);
		return 0;
	}

	if (hash > LONG_MAX)
		return PyLong_FromUnsignedLongLong((unsigned PY_LONG_LONG)hash);

	return PyInt_FromLong((long)hash);
}
