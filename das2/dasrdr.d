module dasrdr;

/** Common utilities needed by many das readers, requires linking with -ldas2 */

import std.process;             // environment
import core.stdc.stdlib : exit;
import std.datetime;
import std.algorithm.searching;
import std.array;
import std.stdio;
import std.format;
import std.bitmanip;
import std.traits;
import std.string; 
import std.conv;
import std.experimental.logger;
import core.stdc.string;
import std.getopt;


public import std.conv: ConvException;

void stop(int nRet){ exit(nRet); }

//////////////////////////////////////////////////////////////////////////////

// Code from terminal.d by Adam Druppe via github,
// License is http://www.boost.org/LICENSE_1_0.txt
//
version(Posix){
	struct winsize {
		ushort ws_row;
		ushort ws_col;
		ushort ws_xpixel;
		ushort ws_ypixel;
	}

	version(linux){
		extern(C) int ioctl(int, int, ...);
		enum int TIOCGWINSZ = 0x5413;
	}
	else version(OSX) {
		extern(C) int ioctl(int, ulong, ...);
		enum TIOCGWINSZ = 1074295912;
	} else static assert(0, "confirm the value of tiocgwinsz");
}

version(Windows){
	import core.sys.windows.windows;
}

/** Get the current size of the terminal
 *
 * Falls back to 80x24 columns if nothing can be determined
 *
 * @return A two element integer array containing [columns, rows].
 */
int[] termSize()
{
	version(Windows) {
		CONSOLE_SCREEN_BUFFER_INFO info;
		GetConsoleScreenBufferInfo( hConsole, &info );

		int cols, rows;

		cols = (info.srWindow.Right - info.srWindow.Left + 1);
		rows = (info.srWindow.Bottom - info.srWindow.Top + 1);

		return [cols, rows];
	}
	else {
		winsize w;
		ioctl(0, TIOCGWINSZ, &w);
		return [w.ws_col, w.ws_row];
	}
}

/** Format getopt options for printing in the style of man page output
 *
 * @param opts A list of options returned from getopt
 * @param width The total print width in columns, used for text wrapping
 * @param indent The number of columns to leave blank before each line
 * @param subIndent The number of columns to leave blank before the help
 *        text of an item.  This is in addition to the overall indention
 * @return a string containing formatted option help text
 */
string formatOptions(Output)(
	Output output, Option[] aOpt, size_t ccTotal, string sIndent, string sSubInd
){
	// cc* - Indicates column count
	string sReq = " (Required)";
	string sPre;
	string sHelp;

	size_t ccOptHdr;
	foreach(opt; aOpt){

		// Assume that the short, long and required strings fit on a line.
		auto prefix = appender!(string)();
		prefix.put(sIndent);
		if(opt.optShort.length > 0){
			prefix.put(opt.optShort);
			if(opt.optLong.length > 0) prefix.put(",");
		}
		prefix.put(opt.optLong);
		if(opt.required) prefix.put(sReq);
		sPre = prefix.data;

		// maybe start option help text on the same line, at least one word of
		// the help text must fit
		if(sPre.length < (sIndent.length + sSubInd.length - 1)){
			sPre = format("%*-s ", (sIndent.length + sSubInd.length - 1), sPre);
			sHelp = wrap(strip(opt.help), ccTotal, sPre, sIndent ~ sSubInd);
		}
		else{
			string sTmp = sIndent~sSubInd;
			sHelp = sPre~"\n"~ wrap(strip(opt.help), ccTotal, sTmp, sTmp);
		}
		output.put(sHelp);
		output.put("\n");
	}

	return output.data;
}

/** Default logger output is too detailed for common usage, provide a logger
 * with cleaner output.
 * Usage:
 *   import std.experimental.logger;
 *   sharedLog = new CleanLogger(stderr, opts.sLogLvl);
 *
 */
class CleanLogger : Logger
{
protected:
	File file_;
	string[ubyte] dLvl;
	
public:
	/** Set the logging level given one of the strings:
	 *
	 *   critical (c)
	 *   error (e)
	 *   warning (w)
	 *   info (i)
	 *   trace (t)
	 *
	 * Only the first letter of the string is significant
	 */
	this(File file, string sLogLvl){
		if(sLogLvl.startsWith('c')) globalLogLevel(LogLevel.critical);
		else if(sLogLvl.startsWith('e')) globalLogLevel(LogLevel.error);
		else if(sLogLvl.startsWith('w')) globalLogLevel(LogLevel.warning);
		else if(sLogLvl.startsWith('i')) globalLogLevel(LogLevel.info);
		else if(sLogLvl.startsWith('d')) globalLogLevel(LogLevel.trace);
		else if(sLogLvl.startsWith('t')) globalLogLevel(LogLevel.trace);
		else if(sLogLvl.startsWith('a')) globalLogLevel(LogLevel.all);
		else globalLogLevel(LogLevel.fatal);
		
		dLvl = [
			LogLevel.all:"ALL", LogLevel.trace:"DEBUG", LogLevel.info:"INFO", 
			LogLevel.warning:"WARNING", LogLevel.error:"ERROR",
			LogLevel.critical:"CRITICAL", LogLevel.fatal:"FATAL",
			LogLevel.off:"OFF"
		];

		super(globalLogLevel()); 
		this.file_ = file;
	}
	
	override void writeLogMsg(ref LogEntry entry){
		auto lt = file_.lockingTextWriter();
      lt.put(dLvl[globalLogLevel()]);
      lt.put(": ");
      lt.put(entry.msg);
      lt.put("\n");
	}	
}


/** Returns a top level working directory for the current project or the empty
 * string.
 */
string getPrefix(){
	string sPrefix = environment.get("PREFIX");
	if(sPrefix is null){
		sPrefix = environment.get("HOME");
		if(sPrefix is null){
			sPrefix = environment.get("USERPROFILE");
			if(sPrefix is null){
				warningf("Cannot determine top level project directory"~
				" tired environment vars PREFIX, HOME, USERPROFILE in that order.");
				return "";
			}
		}
	}
	return sPrefix;
}



/** Use ConvException for error converting times, otherwise this is okay */
class DasException : Exception{
	this(string sMsg){ super(sMsg); }
}



// Link with -ldas2 to get these

extern (C) int parsetime (
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

extern (C) double dt_diff(const das_time_t* pA, const das_time_t* pB);
extern (C) char* dt_isoc(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);
extern (C) char* dt_isod(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);
extern (C) char* dt_dual_str(char* sBuf, size_t nLen, const das_time_t* pDt, int nFracSec);
extern (C) void  dt_tnorm(das_time_t* dt);

extern(C) extern const(char*) UNIT_US2000; /* microseconds since midnight, Jan 1, 2000 */

extern(C) extern const(char*) UNIT_T2000;
extern(C) extern const(char*) UNIT_T1970;

extern(C) double Units_convertFromDt(UnitType epoch_units, const das_time_t* pDt);
extern(C) void Units_convertToDt(das_time_t* pDt, double value, UnitType epoch_units);
extern(C) bool Units_haveCalRep(UnitType unit);
extern(C) const(char*) Units_toStr(UnitType unit);
void tnorm (int *year, int *month, int *mday, int *yday,
            int *hour, int *minute, double *second);

alias UnitType = const(char)*;  // No class for units, just manipulates unsafe 
                               // string pointers

/** Time handling class that drops time zone complexity and sub-second 
 *  integer units.  Has conversions to epoch times */
struct DasTime{
	int year = 1; 
	int month = 1; 
	int mday = 1; 
	int yday = 1;   // Typically read only except for normDoy()
	int hour = 0;   // redundant, but explicit beats implicit
	int minute = 0; // default value for ints is 0 
	double second = 0.0;

	double fEpoch = double.nan;   // Save the epoch value if it has been 
	                             // computed
	UnitType ut = null;

	/** Construct a time value using a string */
	this(const(char)[] s){
		int nRet;
		//infof("Parsting time string: %s", s);
		nRet = parsetime(s.toStringz(),&year,&month,&mday,&yday,&hour,&minute,&second);
		if(nRet != 0)
			throw new ConvException(format("Error parsing %s as a date-time", s));
	}
	
	this(ref das_time_t dt){
		year = dt.year;
		month = dt.month;
		mday = dt.mday;
		yday = dt.yday;
		hour = dt.hour;
		minute = dt.minute;
		second = dt.second;
	}

	void setFromDt(das_time_t* pDt){
		year = pDt.year;
		month = pDt.month;
		mday = pDt.mday;
		yday = pDt.yday;
		hour = pDt.hour;
		minute = pDt.minute;
		second = pDt.second;
	}

	das_time_t toDt() const{
		das_time_t dt;
		dt.year = year;
		dt.month = month;
		dt.mday = mday;
		dt.yday = yday;
		dt.hour = hour;
		dt.minute = minute;
		dt.second = second;
		return dt;
	}

	this(double value, UnitType units){
		das_time_t dt;
		if(! Units_haveCalRep(units)) 
			throw new ConvException(
				format("Unit type %s not convertable to a date-time", Units_toStr(units))
			);
		Units_convertToDt(&dt, value, units);
		setFromDt(&dt);
		ut = units;
		fEpoch = value;
	}

	/** Create a time using a vairable length tuple.
	 * 
	 * Up to 6 arguments will be recognized, at least one must be given
	 * year, month, day, hour, minute, seconds
	 * All items not initialized will recive default values which are
	 *  year = 1, month = 1, day = 1, hour = 0, minute = 0, seconds = 0.0
	 *
	 */
	
	this(T...)(T args){
		static assert(args.length > 0);
		year = args[0];
		static if(args.length > 1) month = args[1];
		static if(args.length > 2) mday = args[2];
		static if(args.length > 3) hour = args[3];
		static if(args.length > 4) minute = args[4];
		static if(args.length > 5) second = args[5];
	}

	double epoch(UnitType units){
		if(ut != units){
			if(! Units_haveCalRep(units)) 
			throw new ConvException(
				format("Unit type %s not convertable to a date-time", Units_toStr(units))
			);
			ut = units;
			das_time_t dt = toDt();
			fEpoch = Units_convertFromDt(units, &dt);
		}
		return fEpoch;
	}

	string toIsoC(int fracdigits) const{
		char[64] aBuf = '\0';
		das_time_t dt = toDt();
		dt_isoc(aBuf.ptr, 63, &dt, fracdigits);
		return aBuf.idup[0..strlen(aBuf.ptr)];
	}

  
	string toString() const{ return toIsoC(6); }
  

  void norm(){
    das_time_t dt = toDt(); 
    dt_tnorm(&dt);
    setFromDt(&dt);
	 if(ut !is null){
		 fEpoch = Units_convertFromDt(ut, &dt);
	 }
  }

	string toIsoD(int fracdigits) const{
		char[64] aBuf = '\0';
		das_time_t dt = toDt();
		dt_isod(aBuf.ptr, 63, &dt, fracdigits);
		return aBuf.idup[0..strlen(aBuf.ptr)];
	}

	string toDual(int fracdigits) const{
		char[64] aBuf = '\0';
		das_time_t dt = toDt();
		dt_dual_str(aBuf.ptr, 63, &dt, fracdigits);
		return aBuf.idup[0..strlen(aBuf.ptr)];
	}

	int opCmp(in DasTime other) const {
		if(year < other.year) return -1; if(year > other.year) return 1;
		if(month < other.month) return -1; if(month > other.month) return 1;
		if(mday < other.mday) return -1; if(mday > other.mday) return 1;
		if(hour < other.hour) return -1; if(hour > other.hour) return 1;
		if(minute < other.minute) return -1; if(minute > other.minute) return 1;
		if(second < other.second) return -1; if(second > other.second) return 1;
		return 0;
	}
};

/** Handles buffering data and prepending proper header ID's for Das2 Headers
 * All output is in UTF-8.
 */
class HdrBuf{

	enum HeaderType { 
		das2 = 1,   /** Output headers without the <?xml version info */
		qstream = 2 /** Include <?xml version declairation on each header packet */
	};
	
	HeaderType m_type;
	string[] m_lText;
	int m_nPktId;
	
	this(int nPktId, HeaderType ht = HeaderType.das2){
		assert(nPktId > -1 && nPktId < 100, format("Invalid Packet ID: %s", nPktId));
		
		m_nPktId = nPktId;
		m_type = ht;
		if(m_type == HeaderType.qstream)
			m_lText[0] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	}
	
	void add(in string sText){ 	m_lText ~= sText;  }
	
	void addf(T...)(T args) { m_lText ~= format(args); }
	
	void send(File fOut){
		string sOut = join(m_lText);
		fOut.writef("[%02d]%06d%s", m_nPktId, sOut.length, sOut);
		fOut.flush();
		m_lText.length = 0;
		if(m_type == HeaderType.qstream)
			m_lText[0] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	}
}
/** Handles outputting data packets for das2 streams and qstreams	*/

class PktBuf{
	enum Endian { big = 1, little = 2};
		
	int m_nPktId;
	ubyte[][] m_aData;
	Endian m_endian = Endian.little;

private:
	final void startPkt(){
		string s = format(":%02d:", m_nPktId);
		m_aData.length = 1;
		m_aData[0] = new ubyte[s.length];
		for(int i = 0; i < s.length; i++) m_aData[0][i] = s[i];	
	}

public:	
	
	this(int nPktId){
		assert(nPktId > 0 && nPktId < 100, format("Invalid Packet ID: %s", nPktId));
		m_nPktId = nPktId;
		startPkt();
	}
	
	void encodeLittleEndian(){
		m_endian = Endian.little;
	}
	void encodeBigEndian(){
		m_endian = Endian.big;
	}
		
	void add(immutable(ubyte)[] uBytes){ 
		size_t u = m_aData.length;
		for(int i = 0; i < uBytes.length; ++i) m_aData[u][i] = uBytes[i];
	}
	
	void addf(T...)(T args) { 
		string s = format(args);
		m_aData.length += 1;
		m_aData[$-1] = new ubyte[s.length];
		for(int i = 0; i < s.length; i++) m_aData[$-1][i] = s[i];
	}
	
	void addFloats(T)(in T[] lNums) 
	     if (isAssignable!(T, float))
	{
		float val;
		ubyte[4] bytes;
		ubyte[] allBytes = new ubyte[ lNums.length * 4 ];
		
		foreach(int i, T t; lNums){
			val = t;
			if(m_endian == Endian.little) bytes = nativeToLittleEndian(val);
			else bytes = nativeToBigEndian(val);
			
			for(int j; j < 4; j++) allBytes[i*4 + j] = bytes[j];
		}
		
		m_aData.length += 1;
		m_aData[$-1] = allBytes;
	}

	void addDoubles(T)(in T[] lNums) 
	     if (isAssignable!(T, double))
	{
		double val;
		ubyte[8] bytes;
		ubyte[] allBytes = new ubyte[ lNums.length * 8 ];
		
		foreach(int i, T t; lNums){
			val = t;
			if(m_endian == Endian.little) bytes = nativeToLittleEndian(val);
			else bytes = nativeToBigEndian(val);
			
			for(int j; j < 8; j++) allBytes[i*8 + j] = bytes[j];
		}
		
		m_aData.length += 1;
		m_aData[$-1] = allBytes;
	}
	
	void send(File fOut){
		ubyte[] uOut = join(m_aData);
		fOut.rawWrite(uOut);
		fOut.flush();
		m_aData.length = 0;
		startPkt();
	}
}


immutable char[] DAS2_EXCEPT_NODATA = "NoDataInInterval";
immutable char[] DAS2_EXCEPT_BADARG = "IllegalArgument";
immutable char[] DAS2_EXCEPT_SRVERR = "ServerError";

/** Send a formatted Das2 exception
 * Params:
 *  fOut = The file object to receive the XML error packet
 *  sType = The exception type. Use one of the pre-defined strings
 *          DAS2_EXCEPT_NODATA
 *          DAS2_EXCEPT_BADARG
 *          DAS2_EXCEPT_SRVERR
 *  sMsg = The error message
 */
void sendException(File fOut, string sType, string sMsg){
	auto sFmt = "<exception type=\"%s\" message=\"%s\" />\n";
	sMsg = sMsg.replace("\n", "&#13;&#10;").replace("\"", "'");		  
	auto sOut = format(sFmt, sType.replace("\"", "'"), sMsg);
	fOut.writef("[xx]%06d%s", sOut.length, sOut);
}

/* ************************************************************************ */
void sendNoData(File fOut, DasTime dtBeg, DasTime dtEnd){
	auto buf = new HdrBuf(0);
	auto sMsg = format("No data in the interval %s to %s", 
	                   dtBeg.toIsoC(3), dtEnd.toIsoC(3));
	warning(sMsg);
	sendException(fOut, "NoDataInInterval", sMsg);
}


/* ************************************************************************ */
/* Access das2 power spectral density estimator */

struct dft_plan;
alias DftPlan = dft_plan;

extern(C) DftPlan* new_DftPlan(size_t uLen, bool bForward);
bool del_DftPlan(DftPlan* pThis);

struct das2_dft_t{
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

extern (C) Das2Dft* new_Dft(DftPlan* pPlan, const char* sWindow);
extern (C) void del_Dft(Das2Dft* pThis);
extern (C) int Dft_calculate(
	Das2Dft* pThis, const double* pReal, const double* pImg
);
extern (C) const (double)* Dft_getReal(Das2Dft* pThis, size_t* pLen);
extern (C) const (double)* Dft_getImg(Das2Dft* pThis, size_t* pLen);
extern (C) const (double)* Dft_getMagnitude(Das2Dft* pThis, size_t* pLen);

/* Builder convienance function, warning it saves @b everything  */
/* extern (C) CorDs** build_from_stdin(char* sProgName, size_t* pSets); */




