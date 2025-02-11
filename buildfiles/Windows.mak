# Libdas2 Makefile for MS Visual C compiler

MAKE=nmake /nologo
CC=cl.exe /nologo

TARG=das3.0

# Special environment variables on Windows that should not be overwritten
# INCLUDE
#

# Put wisdom file for this computer in %CommonProgramFiles%\fftw\wisdom.dat

# Deal with differences from building under anaconda
!if defined(CONDA_BUILD_STATE)
INC=/I . /I $(LIBRARY_INC)
ED=$(LIBRARY_LIB)
EXPAT_LIB=$(ED)\libexpat.lib
INSTALL_PREFIX=$(PREFIX)
!else
INC=/I . /I $(LIBRARY_INC)
ED=$(LIBRARY_LIB)
EXPAT_LIB=$(ED)\libexpatMD.lib
!endif
EX_LIBS=$(EXPAT_LIB) $(ED)\fftw3.lib $(ED)\zlib.lib $(ED)\libssl.lib $(ED)\libcrypto.lib Advapi32.lib User32.lib Crypt32.lib ws2_32.lib $(ED)\pthreadVC3.lib

CFLAGS=$(CFLAGS) /Z7 /DWISDOM_FILE=C:/ProgramData/fftw3/wisdom.dat $(INC)
LFLAGS=/link /DEBUG


SD=das2
BD=build.windows

SRCS=$(SD)\das1.c $(SD)\array.c $(SD)\buffer.c $(SD)\builder.c $(SD)\cli.c \
  $(SD)\codec.c $(SD)\credentials.c $(SD)\dataset.c $(SD)\dataset_hdr2.c \
  $(SD)\dataset_hdr3.c $(SD)\datum.c $(SD)\descriptor.c $(SD)\dft.c $(SD)\dimension.c \
  $(SD)\dsdf.c $(SD)\encoding.c $(SD)\frame.c $(SD)\http.c $(SD)\io.c $(LD)\iterator.c \
  $(SD)\json.c $(SD)\log.c $(SD)\node.c $(SD)\oob.c $(SD)\operator.c $(SD)\packet.c \
  $(SD)\plane.c $(SD)\processor.c $(SD)\property.c $(SD)\send.c $(SD)\stream.c \
  $(SD)\time.c $(SD)\tt2000.c $(SD)\units.c $(SD)\utf8.c $(SD)\util.c $(SD)\value.c \
  $(SD)\var_base.c $(SD)\var_con.c $(SD)\var_seq.c $(SD)\var_ary.c $(SD)\var_una.c \
  $(SD)\var_bin.c $(SD)\vector.c


LD=$(BD)\static
STATIC_OBJS=$(LD)\das1.obj $(LD)\array.obj $(LD)\buffer.obj $(LD)\builder.obj $(LD)\cli.obj \
  $(LD)\codec.obj $(LD)\credentials.obj $(LD)\dataset.obj $(LD)\dataset_hdr2.obj \
  $(LD)\dataset_hdr3.obj $(LD)\datum.obj $(LD)\descriptor.obj $(LD)\dft.obj $(LD)\dimension.obj \
  $(LD)\dsdf.obj $(LD)\encoding.obj $(LD)\frame.obj $(LD)\http.obj $(LD)\io.obj $(LD)\iterator.obj \
  $(LD)\json.obj $(LD)\log.obj $(LD)\node.obj $(LD)\oob.obj $(LD)\operator.obj $(LD)\packet.obj \
  $(LD)\plane.obj $(LD)\processor.obj $(LD)\property.obj $(LD)\send.obj $(LD)\stream.obj \
  $(LD)\time.obj $(LD)\tt2000.obj $(LD)\units.obj $(LD)\utf8.obj $(LD)\util.obj $(LD)\value.obj \
  $(LD)\var_base.obj $(LD)\var_con.obj $(LD)\var_seq.obj $(LD)\var_ary.obj $(LD)\var_una.obj \
  $(LD)\var_bin.obj $(LD)\vector.obj
  
DD=$(BD)\shared
DLL_OBJS=$(DD)\das1.obj $(DD)\array.obj $(DD)\buffer.obj $(DD)\builder.obj $(DD)\cli.obj \
  $(DD)\codec.obj $(DD)\credentials.obj $(DD)\dataset.obj $(DD)\dataset_hdr2.obj \
  $(DD)\dataset_hdr3.obj $(DD)\datum.obj $(DD)\descriptor.obj $(DD)\dft.obj $(DD)\dimension.obj \
  $(DD)\dsdf.obj $(DD)\encoding.obj $(DD)\frame.obj $(DD)\http.obj $(DD)\io.obj $(DD)\iterator.obj \
  $(DD)\json.obj $(DD)\log.obj $(DD)\node.obj $(DD)\oob.obj $(DD)\operator.obj $(DD)\packet.obj \
  $(DD)\plane.obj $(DD)\processor.obj $(DD)\property.obj $(DD)\send.obj $(DD)\stream.obj \
  $(DD)\time.obj $(DD)\tt2000.obj $(DD)\units.obj $(DD)\utf8.obj $(DD)\util.obj $(DD)\value.obj \
  $(DD)\var_base.obj $(DD)\var_con.obj $(DD)\var_seq.obj $(DD)\var_ary.obj $(DD)\var_una.obj \
  $(DD)\var_bin.obj $(DD)\vector.obj
  
HDRS=$(SD)\das1.h $(SD)\array.h $(SD)\buffer.h $(SD)\builder.h $(SD)\core.h \
  $(SD)\codec.h $(SD)\cli.h $(SD)\credentials.h $(SD)\dataset.h $(SD)\datum.h \
  $(SD)\descriptor.h $(SD)\defs.h $(SD)\dft.h $(SD)\dimension.h $(SD)\dsdf.h \
  $(SD)\encoding.h $(SD)\frame.h $(SD)\http.h $(SD)\io.h $(SD)\iterator.h \
  $(SD)\json.h $(SD)\log.h $(SD)\node.h $(SD)\oob.h $(SD)\operator.h $(SD)\packet.h \
  $(SD)\plane.h $(SD)\processor.h $(SD)\property.h $(SD)\send.h $(SD)\stream.h \
  $(SD)\time.h $(SD)\tt2000.h $(SD)\units.h $(SD)\utf8.h $(SD)\util.h $(SD)\value.h \
  $(SD)\variable.h $(SD)\vector.h

UTIL_PROGS=$(BD)\das1_inctime.exe $(BD)\das2_prtime.exe $(BD)\das1_fxtime.exe \
 $(BD)\das2_ascii.exe $(BD)\das2_bin_avg.exe $(BD)\das2_bin_avgsec.exe \
 $(BD)\das2_bin_peakavgsec.exe $(BD)\das2_cache_rdr.exe $(BD)\das2_from_das1.exe \
 $(BD)\das2_from_tagged_das1.exe $(BD)\das1_ascii.exe $(BD)\das1_bin_avg.exe \
 $(BD)\das2_bin_ratesec.exe $(BD)\das2_psd.exe $(BD)\das2_hapi.exe \
 $(BD)\das2_histo.exe $(BD)\das3_node.exe $(BD)\das3_csv.exe $(BD)\das3_test.exe

TEST_PROGS=$(BD)\TestUnits.exe $(BD)\TestArray.exe $(BD)\TestBuilder.exe \
 $(BD)\TestAuth.exe $(BD)\TestCatalog.exe $(BD)\TestTT2000.exe $(BD)\TestVariable.exe \
 $(BD)\TestCredMngr.exe $(BD)\TestV3Read.exe $(BD)\TestIter.exe
 
# Add in cspice error handling functions if SPICE = yes
!if defined(SPICE)
!  if ! defined(CSPICE_INC)
!    error set CSPICE_INC to the absoute path of the CSpice headers directory first
!  endif
CFLAGS=$(CFLAGS) /I $(CSPICE_INC)
!  if ! defined(CSPICE_LIB)
!    error set CSPICE_LIB to the absolute path to the file cspice.lib first
!  endif
EX_LIBS=$(EX_LIBS) $(CSPICE_LIB)
!  if "$(SPICE)"=="yes"
SRCS=$(SRCS) $(SD)\spice.c
STATIC_OBJS=$(STATIC_OBJS) $(LD)\spice.obj
DLL_OBJS=$(DLL_OBJS) $(DD)\spice.obj
HDR=$(HDRS) $(SD)\spice.h
TEST_PROGS=$(TEST_PROGS) $(BD)\TestSpice.exe
UTIL_PROGS=$(UTIL_PROGS) $(BD)\das3_spice.exe
!  endif
!endif

# Add in CDF error handling functions if CDF = yes
!if defined(CDF)
!  if ! defined(CDF_INC)
!    error set CDF_INC to the absoute path of the CDF headers directory first
!  endif
!  if ! defined(CDF_LIB)
!    error set CDF_LIB to the absolute path to the file libcdf.lib first
!  endif
!  if "$(CDF)"=="yes"
UTIL_PROGS=$(UTIL_PROGS) $(BD)\das3_cdf.exe
!  endif
!endif


build: static shared progs
  
static: $(LD) $(BD)\lib$(TARG).lib 
	
shared: $(DD) $(BD)\$(TARG).dll $(BD)\$(TARG).lib

progs: $(TEST_PROGS) $(UTIL_PROGS)

run_test:
	$(BD)\TestUnits.exe
	$(BD)\TestTT2000.exe
	$(BD)\TestArray.exe
	$(BD)\TestVariable.exe
	$(BD)\TestCatalog.exe
	$(BD)\TestBuilder.exe
	$(BD)\das3_test test\cassini_rpws_wfrm_sample.d2s
	$(BD)\TestCredMngr.exe $(BD)
	$(BD)\TestV3Read.exe
	$(BD)\TestIter

test_spice:
	$(BD)\TestSpice.exe

# Can't test CDF creation this way due to stupide embedded time stamps
# cmp $(BD)/ex12_sounder_xyz.cdf test/ex12_sounder_xyz.cdf
test_cdf:
	@echo "INFO: Testing CDF creation"
	$(BD)\das3_cdf -l warning -i test\ex12_sounder_xyz.d3t -o $(BD) -r 
	@echo "INFO: CDF was created"


$(LD):
	if not exist "$(LD)" mkdir "$(LD)"

$(DD):
	if not exist "$(DD)" mkdir "$(DD)"

$(BD)\lib$(TARG).lib:$(STATIC_OBJS)
	@echo lib /verbose /ltcg $(STATIC_OBJS) /out:$@
	lib /verbose /ltcg $(STATIC_OBJS) /out:$@
	
$(BD)\$(TARG).dll:$(DLL_OBJS)
	link /nologo /ltcg /dll $(DLL_OBJS) $(EX_LIBS) /out:$(BD)\$(TARG).dll /implib:$(BD)\$(TARG).lib

install:
	@if "$(INSTALL_PREFIX)"=="" ( \
		@echo ERROR: Install location ^%INSTALL_PREFIX^% undefined. Set INSTALL_PREFIX before nmake install. \
		@exit /b 3 \
	)
	if not exist $(INSTALL_PREFIX)\bin\$(N_ARCH) mkdir $(INSTALL_PREFIX)\bin\$(N_ARCH)
	if not exist $(INSTALL_PREFIX)\lib\$(N_ARCH) mkdir $(INSTALL_PREFIX)\lib\$(N_ARCH)
	if not exist $(INSTALL_PREFIX)\include\das2 mkdir $(INSTALL_PREFIX)\include\das2
	copy $(BD)\lib$(TARG).lib $(INSTALL_PREFIX)\lib\$(N_ARCH)
	copy $(BD)\$(TARG).dll $(INSTALL_PREFIX)\bin\$(N_ARCH)
	copy $(BD)\$(TARG).lib $(INSTALL_PREFIX)\lib\$(N_ARCH)
	for %I in ( $(HDRS) ) do copy %I $(INSTALL_PREFIX)\include\das2
	for %I in ( $(UTIL_PROGS) ) do copy %I $(INSTALL_PREFIX)\bin\$(N_ARCH)
	
# Override rule for utility programs that need more than one source file
$(BD)\das2_bin_ratesec.exe:utilities\das2_bin_ratesec.c utilities\via.c
	$(CC) $(CFLAGS) /Fe:$@ $** $(EX_LIBS) $(BD)\lib$(TARG).lib $(LFLAGS)

$(BD)\das2_psd.exe:utilities\das2_psd.c utilities\send.c
	$(CC) $(CFLAGS) /Fe:$@ $** $(EX_LIBS) $(BD)\lib$(TARG).lib $(LFLAGS)

$(BD)\das3_cdf.exe:utilities\das3_cdf.c
	$(CC) $(CFLAGS) /I $(CDF_INC) /Fe:$@ $** $(EX_LIBS) $(BD)\lib$(TARG).lib $(CDF_LIB)	$(LFLAGS)

# Inference rule for static lib
{$(SD)\}.c{$(LD)\}.obj:
	$(CC) $(CFLAGS) /Fo:$@ /c $<
	
# Inference rule for DLL files
{$(SD)\}.c{$(DD)\}.obj:
	$(CC) $(CFLAGS) /DDAS_USE_DLL /DBUILDING_DLL /Fo:$@ /c $<
	
# Inference rule for test programs
{test\}.c{$(BD)\}.exe:
	$(CC) $(CFLAGS) /Fe:$@ $< $(EX_LIBS) $(BD)\lib$(TARG).lib $(LFLAGS)

# Inference rule for util programs
{utilities\}.c{$(BD)\}.exe:
	$(CC) $(CFLAGS) /Fe:$@ $< $(EX_LIBS) $(BD)\lib$(TARG).lib $(LFLAGS)
	
clean:
	del *.obj

distclean:
	if exist $(BD) rmdir /S /Q $(BD)
	del *.obj

