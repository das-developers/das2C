module das2.dwrap;

// D Language wrappers for libdas2.
//
//  Note: This is not a systematic definition of all usable items in liddas2.a.  Instead 
//        definitions are added as needed.  If you are working on a D-language program
//        that uses something in libdas2 that is not here, go ahead and add it.  Ask
//        chris-piker@uiowa.edu if you need help.
//
// To use libdas2 when compiling D programs, simply include this module file on the
// compiler command line as well as the libdas2.a object itself.  For example:
//
// dmd your_file1.d your_file2.d /project/include/das2.d /project/lib/libdas2.a
//
// To cut down on the length of the module, most comments are not repeated here, see
// the C-language doxygen comments for details.

// Das Times
extern(C) int parsetime (
	const char *string, int *year, int *month, int *day_month, int *day_year, 
	int *hour, int *minute, double *second
);

struct das_time_t{
	int year; 
	int month; 
	int mday; 
	int yday; 
   int hour;
	int minute; 
	double second;	
};

extern (C) void dt_null(das_time_t* pDt);
extern (C) bool dt_parsetime(const char* string, das_time_t* dt);
extern (C) void dt_copy(das_time_t* pDest, const das_time_t* pSrc);
extern (C) int dt_compare(const das_time_t* pA, const das_time_t* pB);
extern (C) double dt_diff(const das_time_t* pA, const das_time_t* pB);
extern (C) char* dt_isoc(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);
extern (C) char* dt_isod(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);
extern (C) char* dt_dual_str(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);
extern (C) double dt_ttime(const das_time_t* dt);
extern (C) void dt_emitt (double tt, das_time_t* dt);
extern (C) void dt_tnorm(das_time_t* dt);  // Extra reminder, dt.yday is OUTPUT only !


// Units
alias UnitType = const char*;  // No class for units, just manipulates strings

extern (C) UnitType Units_fromStr(const char* string);
extern (C) const (char)* Units_toStr(UnitType unit);
extern (C) char* Units_toLabel(UnitType unit, char* sBuf, int nLen);
extern (C) UnitType Units_invert(UnitType unit);
extern (C) UnitType Units_multiply(UnitType ut1, UnitType ut2);
extern (C) UnitType Units_divide(UnitType a, UnitType b);
extern (C) UnitType Units_power(UnitType unit, int power);
extern (C) UnitType Units_root(UnitType unit, int root );
extern (C) UnitType Units_interval(UnitType unit);
extern (C) bool Units_canConvert(UnitType fromUnits , UnitType toUnits);
extern (C) bool Units_haveCalRep(UnitType unit);
extern (C) void Units_convertToDt(das_time_t* pDt, double value, UnitType epoch_units);
extern (C) double Units_convertFromDt(UnitType epoch_units, const das_time_t* pDt);


// DFT
struct das2_dft_t{
	void* vpPlan;
	void* vpIn;
	void* vpOut;
	size_t uLen;
	bool bRealOnly;
	char* sWindow;
	double* pWnd;
	bool bNewMag;
	double* pMag;
	size_t uMagLen;
	bool[2] bNewCmp;   /* fftw convention: 0 = reals, 1 = img */
	double*[2] pCmpOut;
	size_t[2] uCmpLen;	
};

alias Das2Dft = das2_dft_t;

extern (C) Das2Dft* new_Dft(size_t uLen, const char* sWindow);
extern (C) void del_Dft(Das2Dft* pThis);
extern (C) int Dft_calculate(
	Das2Dft* pThis, const double* pReal, const double* pImg, size_t uLen
);
extern (C) const (double)* Dft_getReal(Das2Dft* pThis, size_t* pLen);
const (double)* Dft_getImg(Das2Dft* pThis, size_t* pLen);


















