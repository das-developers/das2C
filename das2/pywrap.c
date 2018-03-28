#define _POSIX_C_SOURCE 200112L

#include <Python.h>
#include <numpy/arrayobject.h>

/*#ifdef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
*/

#include "das2/das1.h"
#include "das2/util.h"
#include "das2/dft.h"

char* myname = "_das2 python module";

/*****************************************************************************/
/* parsetime */

const char das2help_parsetime[] = 
  "Converts most human-parseable time strings to numeric components.\n"
  "Returns a tuple of the form:\n"
  "\n"
  "   (year, month, mday, yday, hour, minute, float_seconds)\n"
  "\n"
  "All tuple components are integers except for the seconds field, which\n"
  "is a float.\n"
  "\n"
  "If the time is not parsable, a ValueError exception is thrown.\n";

static PyObject* das2_parsetime(PyObject* self, PyObject* args)
{
	const char* sTime;
	int year, month, mday, yday, hour, min;
	double sec;
	
	if(!PyArg_ParseTuple(args, "s:parsetime", &sTime))
		return NULL;
	
	if(parsetime(sTime, &year, &month, &mday, &yday, &hour, &min, &sec) != 0){
		PyErr_SetString(PyExc_ValueError, "String was not parseable as a datetime");
		return NULL;
	}
	
	return Py_BuildValue("(iiiiiid)", year, month, mday, yday, hour, min, sec);
}

const char das2help_ttime[] = 
  "Converts time components to a double precision floating point value\n"
  "(seconds since the beginning of 1958, ignoring leap seconds) and\n"
  "normalize inputs.  Note that this floating point value should only be \n"
  "used for \"internal\" purposes.  (There's no need to propagate yet\n"
  "another time system, plus I want to be able to change/fix these values.)\n"
  "\n"
  "There is no accomodation for calendar adjustments, for example the\n"
  "transition from Julian to Gregorian calendar, so I wouldn't recommend\n"
  "using this routine for times prior to the 1800's.\n"
  "\n"
  "Arguments (will be normalized if necessary):\n"
  "   int year                - year (1900 will be added to two-digit values)\n"
  "   int month (optional)    - month of year (1-12)\n"
  "   int mday (optional)     - day of month (1-31)\n"
  "   int hour (optional)     - hour of day (0-23)\n"
  "   int minute (optional)   - minute of hour (0-59)\n"
  "   float second (optional) - second of minute (0.0 <= s < 60.0), \n"
  "                             leapseconds ignored\n"
  "\n"
  "Note:  To use day of year as input, simple specify 1 for the month and\n"
  "the day of year in place of day of month.  Beware of the normalization.\n";
  
static PyObject* das2_ttime(PyObject* self, PyObject* args)
{
	int year;
	int month = 1;
	int mday = 1;
	int hour = 0;
	int min = 0;
	int ignored = 0;
	double sec = 0.0;
	double dRet;
	
	if(!PyArg_ParseTuple(args, "i|iiiid:ttime", &year, &month, &mday, &hour,
			               &min, &sec))
		return NULL;
	
	dRet = ttime(&year, &month, &mday, &ignored, &hour, &min, &sec);
	
	return Py_BuildValue("d", dRet);
}

const char das2help_emitt[] = 
	"Performs the inverse operation as ttime.  Converts floating point\n"
	"seconds since the beginning of 1958 back into a broken down time \n"
	"tuple:\n"
	"\n"
	"  (year, month, mday, yday, hour, minute, float_seconds)\n";

static PyObject* das2_emitt(PyObject* self, PyObject* args)
{
	double dEpoch;
	int year, month, mday, yday, hour, min;
	double sec;

	if(!PyArg_ParseTuple(args, "d:emitt", &dEpoch))
		return NULL;

	emitt(dEpoch, &year, &month, &mday, &yday, &hour, &min, &sec);
	
	return Py_BuildValue("(iiiiiid)", year, month, mday, yday, hour, min, sec);
}

const char das2help_tnorm[] =
	"Normalizes date and time components\n"
	"Arguments (will be normalized if necessary):\n"
	"   int year                - year (1900 will be added to two-digit values)\n"
	"   int month (optional)    - month of year (1-12)\n"
	"   int mday (optional)     - day of month (1-31)\n"
	"   int hour (optional)     - hour of day (0-23)\n"
	"   int minute (optional)   - minute of hour (0-59)\n"
	"   float second (optional) - second of minute (0.0 <= s < 60.0), \n"
	"                             leapseconds ignored\n"
	"\n"
	"Note:  To use day of year as input, simple specify 1 for the month and\n"
	"the day of year in place of day of month.  Beware of the normalization.\n"
	"Returns a tuple of the form:\n"
	"\n"
	"   (year, month, mday, yday, hour, minute, float_seconds)\n";

static PyObject* das2_tnorm(PyObject* self, PyObject* args)
{
	int year;
	int month = 0;
	int mday = 0;
	int yday = 0;
	int hour = 0;
	int min = 0;
	double sec = 0.0;

	if(!PyArg_ParseTuple(args, "i|iiiid:tnorm", &year, &month, &mday,
						 &hour, &min, &sec))
		return NULL;

	tnorm(&year, &month, &mday, &yday, &hour, &min, &sec);

	return Py_BuildValue("(iiiiiid)", year, month, mday, yday, hour, min, sec);
}


/*****************************************************************************/
/* The method defintions */

static PyMethodDef das2_methods[] = {
	{"parsetime", das2_parsetime, METH_VARARGS, das2help_parsetime },
	{"ttime",     das2_ttime,     METH_VARARGS, das2help_ttime     },
	{"emitt",     das2_emitt,     METH_VARARGS, das2help_emitt     },
	{"tnorm",     das2_tnorm,     METH_VARARGS, das2help_tnorm     },
	{NULL, NULL, 0, NULL}
};

/*****************************************************************************/
/* Dft type definition */

/* instance structure */
typedef struct {
	PyObject_HEAD
	Das2Dft* das2dft;
} das2_Dft;

static void das2_Dft_dealloc(das2_Dft* self) {
	if (self->das2dft)
		del_Dft(self->das2dft);
	self->ob_type->tp_free((PyObject*)self);
}

static PyObject* das2_Dft_new(
	PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	das2_Dft* self;

	self = (das2_Dft*)type->tp_alloc(type, 0);
	if (self != NULL) {
		self->das2dft = NULL;
	}

	return (PyObject*)self;

}

static int das2_Dft_init(das2_Dft* self, PyObject *args, PyObject *kwds) {

	char *sWindow = NULL;
	unsigned int uLen = 0;
	Das2ErrorMessage* errMsg;

	static char *kwlist[] = {"uLen", "sWindow", NULL};

	if (! PyArg_ParseTupleAndKeywords(args, kwds, "Iz", kwlist,
		&uLen, &sWindow))
	{
		return -1;
	}		
	
	if ( self->das2dft != NULL ) {
		del_Dft(self->das2dft);
		self->das2dft = NULL;
	}
	
	self->das2dft = new_Dft(uLen, sWindow, true);

	if ( self->das2dft == NULL ) {
		errMsg = das2_get_error();
		PyErr_SetString(PyExc_ValueError, errMsg->message);
		return -1;
	}

	return 0;
}

const char das2help_Dft_calculate[] =
	"Calculate a discrete Fourier transform.\n"
	"\n"
	"Using the calculation plan setup in the constructor, calculate a\n"
	"discrete Fourier transform.  When this function is called internal\n"
	"storage of any previous DFT calculations (if any) are over written.\n"
	"	Arguments\n"
	"		pReal    A \"time domain\" input vector\n"
	"		pImg     The imaginary (or quadrature phase) input vector. For \n"
	"		         a purely real signal this vector is None\n"
	"A ValueError is thrown if  pImg is not None and a different length\n"
	"             than pReal\n"
	"A ValueError is thrown if the length of pImg is odd or less than 2.\n";

static PyObject* das2_Dft_calculate(das2_Dft* self, PyObject* args) {
	PyObject* pReal = NULL;
	PyObject* pImg = Py_None;
	PyObject* arrReal = NULL;
	PyObject* arrImg = NULL;
	double* dReal;
	double* dImg;
	size_t uLen;
	ErrorCode err;
	Das2ErrorMessage* errMsg;
	char* tmp;
	

	if (!PyArg_ParseTuple(args, "O|O:calculate", &pReal, &pImg)) {
		return NULL;
	}

	arrReal = PyArray_FROM_OTF(pReal, NPY_DOUBLE, NPY_IN_ARRAY);
	if (arrReal == NULL) {
		return NULL;
	}

	if (pImg == Py_None) {
		arrImg = Py_None;
		Py_INCREF(Py_None);
	}
	else {
		arrImg = PyArray_FROM_OTF(pImg, NPY_DOUBLE, NPY_IN_ARRAY);
		if (arrImg == NULL) {
			Py_DECREF(arrReal);
			return NULL;
		}
	}

	if ( PyArray_NDIM(arrReal) != 1 ) {
		PyErr_SetString(PyExc_ValueError, "pReal is not 1-dimensional");
		Py_DECREF(arrReal);
		Py_DECREF(arrImg);
		return NULL;
	}
	if ( arrImg != Py_None && PyArray_NDIM(arrImg) != 1 ) {
		PyErr_SetString(PyExc_ValueError, "pImg is not 1-dimensional");
		Py_DECREF(arrReal);
		Py_DECREF(arrImg);
		return NULL;
	}

	uLen = PyArray_Size(arrReal);
	if ( arrImg != Py_None && uLen != PyArray_Size(arrImg) ) {
		PyErr_SetString(PyExc_ValueError, "pReal and pImg must be the same length");
		Py_DECREF(arrReal);
		Py_DECREF(arrImg);
		return NULL;
	}

	dReal = (double*)PyArray_DATA(arrReal);
	if (arrImg == Py_None) {
		dImg = NULL;
	}
	else {
		dImg = (double*)PyArray_DATA(arrImg);
	}

	err = Dft_calculate(self->das2dft,dReal,dImg,uLen);
	if ( err != DAS_OKAY ) {
		errMsg = das2_get_error();
		if (err == errMsg->nErr) {
			tmp = errMsg->message;
		}
		else {
			tmp = "Unknown error";
		}
		PyErr_SetString(PyExc_ValueError, tmp);
	}

	Py_DECREF(arrReal);
	Py_DECREF(arrImg);
	
	Py_RETURN_NONE;
}

const char das2help_Dft_getReal[] =
	"Return the real component after a calculation.";

static PyObject* das2_Dft_getReal(das2_Dft* self, PyObject* noargs) {
	PyObject* arrReal;
	size_t pLen;
	const double* real;
	npy_intp dims;

	real = Dft_getReal(self->das2dft,&pLen);

	dims = pLen;
	arrReal = (PyObject*)PyArray_SimpleNew(1,&dims,NPY_DOUBLE);
	if (arrReal==NULL) {
		return NULL;
	}

	memcpy(PyArray_DATA(arrReal),real,sizeof(double)*pLen);

	return arrReal;
}

const char das2help_Dft_getImg[] =
	"Return the imaginary component after a calculation.";

static PyObject* das2_Dft_getImg(das2_Dft* self, PyObject* noargs) {
	PyObject* arrImg;
	size_t pLen;
	const double *img;
	npy_intp dims;

	img = Dft_getImg(self->das2dft,&pLen);

	dims = pLen;
	arrImg = (PyObject*)PyArray_SimpleNew(1,&dims,NPY_DOUBLE);
	if (arrImg==NULL) {
		return NULL;
	}

	memcpy(PyArray_DATA(arrImg),img,sizeof(double)*pLen);

	return arrImg;
}

const char das2help_Dft_getMagnitude[] =
	"Get the amplitude magnitude vector from a calculation.\n"
	"\n"
	"Scale the stored DFT so that it preserves amplitude, and get the\n"
	"magnitude. For real-valued inputs (complex pointer = 0) the 'positive'\n"
	"and 'negative' frequencies are combined.  For complex input vectors\n"
	"this is not the case since all DFT output amplitudes are unique.\n"
	"Stated another way, for complex input signals components above the\n"
	"Nyquist frequency have meaningful information.";

static PyObject* das2_Dft_getMagnitude(das2_Dft* self, PyObject* noargs) {
	PyObject* arrMagn;
	size_t pLen;
	const double *magn;
	npy_intp dims;
	
	magn = Dft_getMagnitude(self->das2dft,&pLen);

	dims = pLen;
	arrMagn = (PyObject*)PyArray_SimpleNew(1,&dims,NPY_DOUBLE);
	if (arrMagn==NULL) {
		return NULL;
	}

	memcpy(PyArray_DATA(arrMagn),magn,sizeof(double)*pLen);

	return arrMagn;
}

const char das2help_Dft_getLength[] =
	"The length of the data vectors that will be supplied to the\n"
	"calculate function.\n";

static PyObject* das2_Dft_getLength(das2_Dft* self, PyObject* noargs) {
	return Py_BuildValue("I",self->das2dft->uLen);
}

static PyMethodDef das2_Dft_methods[] = {
	{"calculate", (PyCFunction)das2_Dft_calculate, METH_VARARGS, das2help_Dft_calculate},
	{"getReal", (PyCFunction)das2_Dft_getReal, METH_NOARGS, das2help_Dft_getReal},
	{"getImg", (PyCFunction)das2_Dft_getImg, METH_NOARGS, das2help_Dft_getImg},
	{"getMagnitude", (PyCFunction)das2_Dft_getMagnitude, METH_NOARGS, das2help_Dft_getMagnitude},
	{"getLength", (PyCFunction)das2_Dft_getLength, METH_NOARGS, das2help_Dft_getLength},
	{NULL,NULL,0,NULL} /* Sentinel */
};

const char das2help_Dft[] =
	"An amplitude preserving Discrete Fourier Transform converter"
	"\n"
	"__init__(nLen, sWindow)\n"
	"	Create a new DFT calculator\n"
	"\n"
	"		nLen	The length of the data vectors that will be supplied\n"
	"				to the calculate function\n"
	"		sWindow	A named window to apply to the data.  If None then\n"
	"				no window will be used.\n"
	"				Accepted values are ['HANN', None]\n";

static PyTypeObject das2_DftType = {
	PyObject_HEAD_INIT(NULL)
	0,							/*ob_size*/
	"das2.Dft",					/*tp_name*/
	sizeof(das2_Dft),			/*tp_basicsize*/
	0,							/*tp_itemsize*/
	(destructor) das2_Dft_dealloc,/*tp_dealloc*/
	0,							/*tp_print*/
	0,							/*tp_getattr*/
	0,							/*tp_setattr*/
	0,							/*tp_compare*/
	0,							/*tp_repr*/
	0,							/*tp_as_number*/
	0,							/*tp_as_sequence*/
	0,							/*tp_as_mapping*/
	0,							/*tp_hash*/
	0,							/*tp_call*/
	0,							/*tp_str*/
	0,							/*tp_getattro*/
	0,							/*tp_setattro*/
	0,							/*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,			/*tp_flags*/
	das2help_Dft,				/*tp_doc*/
	0,							/*tp_travsere*/
	0,							/*tp_clear*/
	0,							/*tp_richcompare*/
	0,							/*tp_weaklistoffset*/
	0,							/*tp_iter*/
	0,							/*tp_iternext*/
	das2_Dft_methods,			/*tp_methods*/
	0,							/*tp_members*/
	0,							/*tp_getset*/
	0,							/*tp_base*/
	0,							/*tp_ditc*/
	0,							/*tp_descr_get*/
	0,							/*tp_descr_set*/
	0,							/*tp_dictoffset*/
	(initproc)das2_Dft_init,	/*tp_init*/
	0,							/*tp_alloc*/
	das2_Dft_new,				/*tp_new*/
};

/*****************************************************************************/
/* Psd type definition */

/* instance structure */
typedef struct {
	PyObject_HEAD
	Das2Psd* das2psd;
} das2_Psd;

static void das2_Psd_dealloc(das2_Psd* self) {
	if (self->das2psd)
		del_Das2Psd(self->das2psd);
	self->ob_type->tp_free((PyObject*)self);
}

static PyObject* das2_Psd_new(
	PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	das2_Psd* self;
	
	self = (das2_Psd*)type->tp_alloc(type, 0);
	if (self != NULL) {
		self->das2psd=NULL;
	}

	return (PyObject*)self;

}

static int das2_Psd_init(das2_Psd* self, PyObject *args, PyObject *kwds) {

	PyObject* pyCenter = NULL;
	char *sWindow = NULL;
	bool bCenter = false;
	unsigned int uLen = 0;
	Das2ErrorMessage* errMsg;

	static char *kwlist[] = {"uLen", "bCenter", "sWindow", NULL};

	if (! PyArg_ParseTupleAndKeywords(args, kwds, "IOz", kwlist,
		&uLen, &pyCenter, &sWindow))
	{
		return -1;
	}

	if (PyObject_IsTrue(pyCenter)) bCenter = true;
	else bCenter = false;
	
	if ( self->das2psd != NULL ) {
		del_Das2Psd(self->das2psd);
		self->das2psd = NULL;
	}

	self->das2psd = new_Psd(uLen, bCenter, sWindow);

	if ( self->das2psd == NULL) {
		errMsg = das2_get_error();
		PyErr_SetString(PyExc_ValueError, errMsg->message);
		return -1;
	}

	return 0;
}

const char das2help_Psd_calculate[] =
	"Calculate a Power Spectral Density (periodogram)\n"
	"\n"
	"Using the calculation plan setup in the constructor, calculate a\n"
	"discrete Fourier transform. When this function is called, internal\n"
	"storage of any previous DFT calculations (if any) are overwritten\n"
	"	pReal	A \"time domain\" input vector\n"
	"	pImg	The imaginary (or quadrature phase) input vector the same\n"
	"			length as pReal. For a purely real signal this vector is\n"
	"			None.\n";

static PyObject* das2_Psd_calculate(das2_Psd* self, PyObject* args) {
	PyObject* pReal = NULL;
	PyObject* pImg = Py_None;
	PyObject* arrReal = NULL;
	PyObject* arrImg = NULL;
	double* dReal;
	double* dImg;
	size_t uLen;
	ErrorCode err;
	Das2ErrorMessage* errMsg;
	char* tmp;

	if (!PyArg_ParseTuple(args, "O|O:calculate", &pReal, &pImg)) {
		return NULL;
	}

	arrReal = PyArray_FROM_OTF(pReal, NPY_DOUBLE, NPY_IN_ARRAY);
	if (arrReal == NULL) {
		return NULL;
	}

	if (pImg == Py_None) {
		arrImg = Py_None;
		Py_INCREF(Py_None);
	}
	else {
		arrImg = PyArray_FROM_OTF(pImg, NPY_DOUBLE, NPY_IN_ARRAY);
		if (arrImg == NULL) {
			Py_DECREF(arrReal);
			return NULL;
		}
	}

	if ( PyArray_NDIM(arrReal) != 1) {
		PyErr_SetString(PyExc_ValueError, "pReal is not 1-dimensional");
		Py_DECREF(arrReal);
		Py_DECREF(arrImg);
		return NULL;
	}
	if ( arrImg != Py_None && PyArray_NDIM(arrImg) != 1) {
		PyErr_SetString(PyExc_ValueError, "pImg is not 1-dimensional");
		Py_DECREF(arrReal);
		Py_DECREF(arrImg);
		return NULL;\
	}

	uLen = PyArray_Size(arrReal);
	if ( arrImg != Py_None && uLen != PyArray_Size(arrImg) ) {
		PyErr_SetString(PyExc_ValueError, "pReal and pImg must be the same length");
		Py_DECREF(arrReal);
		Py_DECREF(arrImg);
		return NULL;
	}

	dReal = (double*)PyArray_DATA(arrReal);
	if (arrImg == Py_None) {
		dImg = NULL;
	}
	else {
		dImg = (double*)PyArray_DATA(arrImg);
	}

	err = Psd_calculate(self->das2psd,dReal,dImg,uLen);
	if ( err != DAS_OKAY ) {
		errMsg = das2_get_error();
		if (err == errMsg->nErr) {
			tmp = errMsg->message;
		}
		else {
			tmp = "Unknown error";
		}
		PyErr_SetString(PyExc_ValueError, tmp);
	}

	Py_DECREF(arrReal);
	Py_DECREF(arrImg);

	Py_RETURN_NONE;
}

const char das2help_Psd_powerRatio[] =
	"Provide a comparison of the input power and the output power.\n"
	"\n"
	"During the calculate() call the average magnitude of the input vector\n"
	"is saved along with the average magnitude of the output vector (divided\n"
	"by the Window summed and squared).  These two measures of power should\n"
	"always be close to each other when using a hann window.  When using a\n"
	"NULL window they should be almost identical, to within rounding error.\n"
	"The two measures are:\n"
	"                N-1\n"
	"            1  ----   2      2\n"
	"    Pin =  --- \\    r    +  i\n"
	"            N  /     n       n\n"
	"               ----\n"
	"                n=0\n"
	"  \n"
	"                  N-1\n"
	"             1   ----   2      2\n"
	"    Pout =  ---  \\    R    +  I\n"
	"            Wss  /     k       k\n"
	"                 ----\n"
	"                  k=0\n"
	"\n"
	"	Arguments:\n"
	"		input	(optional) if True include the input power in the return\n"
	"		output	(optional) if True include the output power in the return\n"
	"\n"
	"	returns	ratio of power out divided by power in (with no parameters)\n"
	"			(inputPower, powerRatio) with input=True\n"
	"			(outputPower, powerRatio) with output=True\n"
	"			(inputPoser, outputPoser, powerRatio) with input=True and\n"
	"			output=True\n";


static PyObject* das2_Psd_powerRatio(const das2_Psd* self, PyObject* args, PyObject* kwds) {

	PyObject* input = Py_False;
	PyObject* output = Py_False;
	double inputPower;
	double outputPower;
	double powerRatio;
	
	static char *kwlist[] = {"input", "output", NULL};

	if (! PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kwlist,
		&input, &output))
	{
		return NULL;
	}

	if (PyObject_IsTrue(input) && PyObject_IsTrue(output)) {
		powerRatio=Psd_powerRatio(self->das2psd, &inputPower, &outputPower);
		return Py_BuildValue("ddd", inputPower, outputPower, powerRatio);
	}
	else if (PyObject_IsTrue(input)) {
		powerRatio=Psd_powerRatio(self->das2psd, &inputPower, NULL);
		return Py_BuildValue("dd", inputPower, powerRatio);
	}
	else if (PyObject_IsTrue(output)) {
		powerRatio=Psd_powerRatio(self->das2psd, NULL, &outputPower);
		return Py_BuildValue("dd", outputPower, powerRatio);
	}
	else {
		powerRatio=Psd_powerRatio(self->das2psd, NULL, NULL);
		return Py_BuildValue("d", powerRatio);
	}
}

const char das2help_Psd_get[] =
	"Get the amplitude magnitude vector from a calculation\n"
	"\n"
	"Scale the stored DFT so that is preserves amplitude, and get the\n"
	"magnitude. For real-value inputs (complex pointer = 0) the 'positive'\n"
	"and 'negetive' frequencies are combined. For complex input vectors this\n"
	"is not the case since all DFT output amplitudes are unique. Stated\n"
	"another way, for complex input signals components above the Nyquist\n"
	"frequency have meaningful information.\n"
	"\n"
	"	return	A pynum array holding the real signal magnitude values\n";

static PyObject* das2_Psd_get(const das2_Psd* self, PyObject* noargs) {
	
	PyObject* arrPsd;
	size_t pLen;
	const double* psd;
	npy_intp dims;

	psd = Psd_get(self->das2psd, &pLen);

	dims = pLen;
	arrPsd = (PyObject*)PyArray_SimpleNew(1,&dims,NPY_DOUBLE);
	if (arrPsd==NULL) {
		return NULL;
	}

	memcpy(PyArray_DATA(arrPsd),psd,sizeof(double)*pLen);

	return arrPsd;
}

static PyMethodDef das2_Psd_methods[] = {
	{"calculate", (PyCFunction)das2_Psd_calculate, METH_VARARGS, das2help_Psd_calculate},
	{"powerRatio", (PyCFunction)das2_Psd_powerRatio, METH_VARARGS|METH_KEYWORDS, das2help_Psd_powerRatio},
	{"get", (PyCFunction)das2_Psd_get, METH_NOARGS, das2help_Psd_get},
	{NULL,NULL,0,NULL} /* Sentinel */
};

const char das2help_Psd[] = 
	"Create a new Power Spectral Density calculator.\n"
	"\n"
	"This estimator uses the equations given in Numerical Recipes in C,\n"
	"section 13.4, but not any of the actual Numerical Recipes source code.\n"
	"\n"
	"__init__(nLen, bCenter, sWindow)\n"
	"	Create a new DFT calculator\n"
	"\n"
	"		nLen	The length of the data vectors that will be supplied\n"
	"				to the calculate function\n"
	"		bCenter	If true, input values will be centered on the Mean value.\n"
	"				This shifts-out the DC component from the input\n"
	"		sWindow	A named window to apply to the data.  If None then\n"
	"				no window will be used.\n"
	"				Accepted values are ['HANN', None]\n";

static PyTypeObject das2_PsdType = {
	PyObject_HEAD_INIT(NULL)
	0,							/*ob_size*/
	"das2.Psd",					/*tp_name*/
	sizeof(das2_Psd),			/*tp_basicsize*/
	0,							/*tp_itemsize*/
	(destructor) das2_Psd_dealloc,/*tp_dealloc*/
	0,							/*tp_print*/
	0,							/*tp_getattr*/
	0,							/*tp_setattr*/
	0,							/*tp_compare*/
	0,							/*tp_repr*/
	0,							/*tp_as_number*/
	0,							/*tp_as_sequence*/
	0,							/*tp_as_mapping*/
	0,							/*tp_hash*/
	0,							/*tp_call*/
	0,							/*tp_str*/
	0,							/*tp_getattro*/
	0,							/*tp_setattro*/
	0,							/*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT,			/*tp_flags*/
	das2help_Psd,				/*tp_doc*/
	0,							/*tp_traverse*/
	0,							/*tp_clear*/
	0,							/*tp_richcompare*/
	0,							/*tp_weaklistoffset*/
	0,							/*tp_iter*/
	0,							/*tp_iternext*/
	das2_Psd_methods,			/*tp_methods*/
	0,							/*tp_members*/
	0,							/*tp_getset*/
	0,							/*tp_base*/
	0,							/*tp_dict*/
	0,							/*tp_descr_get*/
	0,							/*tp_descr_set*/
	0,							/*tp_dictoffset*/
	(initproc)das2_Psd_init,	/*tp_init*/
	0,							/*tp_alloc*/
	das2_Psd_new,				/*tp_new*/
};

/*****************************************************************************/
/* Module initialization */
PyMODINIT_FUNC init_das2(void){

	PyObject* m;

	das2_save_error(512);
	das2_return_on_error();

	if (PyType_Ready(&das2_DftType) < 0)
		return;
	if (PyType_Ready(&das2_PsdType) < 0)
		return;

	m = Py_InitModule3("_das2", das2_methods, "daslib with extensions");


	/* This statement is required to setup the numpy C API
 	 * If you leave it out you WILL get SEGFAULTS
 	 */
	import_array();

	Py_INCREF(&das2_DftType);
	PyModule_AddObject(m, "Dft", (PyObject *)&das2_DftType);
	Py_INCREF(&das2_PsdType);
	PyModule_AddObject(m, "Psd", (PyObject *)&das2_PsdType);
}

