# das2C makefile for the Microsoft Visual C compiler and nmake
#
# Run from the top of the source tree in a 64-bit build environment, i.e.
# after vcvars64.bat, or in an "x64 Native Tools Command Prompt":
#
#   nmake /f buildfiles\Windows.mak               Static library and programs
#   nmake /f buildfiles\Windows.mak test          Build, then run the tests
#   nmake /f buildfiles\Windows.mak dll           das3.dll and import lib das3.lib
#   nmake /f buildfiles\Windows.mak PREFIX=C:\opt install      Programs, schemas
#   nmake /f buildfiles\Windows.mak PREFIX=C:\opt dev_install  Adds lib, headers
#   nmake /f buildfiles\Windows.mak PREFIX=C:\opt dll_install  DLL, import lib, headers
#
# Add SPICE=yes and/or CDF=yes to any of the above to build in the SPICE and
# CDF programs.
#
# Dependencies are found or fetched as follows:
#
#   expat, fftw3, openssl, pthreads, zlib:  From LIBRARY_INC and LIBRARY_LIB
#      when set (conda sets these), otherwise vcpkg installs the packages in
#      vcpkg.json under build.windows\vcpkg_installed.  This needs VCPKG_ROOT
#      to point at a vcpkg clone.
#
#   CSPICE:  From CSPICE_INC and CSPICE_LIB when set, otherwise the NAIF
#      toolkit is downloaded to build.windows\cspice
#
#   CDF:  From CDF_INC and CDF_LIB when set, otherwise the SPDF distribution
#      is downloaded to build.windows\cdf
#
# nmake reads every macro before running any command, so library names that
# vary between dependency versions can't be checked until the dependencies
# are installed.  Thus the public targets fetch dependencies first and then
# run nmake again for the actual build.

THIS=buildfiles\Windows.mak
MAKE=nmake /nologo
CC=cl.exe /nologo

TARG=das3

SD=das3

# The build directory also appears literally in the list substitutions below,
# since nmake doesn't expand macros inside a substitution
BD=build.windows
LD=$(BD)\static
DD=$(BD)\shared

# Built-in curl and tar, Windows 10 and up.  Named in full because Git for
# Windows, when its Unix tools are on the path, can put a GNU tar that doesn't
# read zip files ahead of the system one.
CURL=$(SYSTEMROOT)\System32\curl.exe -f -L
TAR=$(SYSTEMROOT)\System32\tar.exe

!include buildfiles\sources.mak

##############################################################################
# Dependency locations

VCPKG_TRIPLET=x64-windows-static
VCPKG_DIR=$(BD)\vcpkg_installed
VCPKG_STAMP=$(VCPKG_DIR)\vcpkg.json.stamp

LOC_CSPICE_URL=https://naif.jpl.nasa.gov/pub/naif/toolkit//C/PC_Windows_VisualC_64bit/packages/cspice.zip
LOC_CDF_URL=https://spdf.gsfc.nasa.gov/pub/software/cdf/dist/cdf39_1/windows/cdf3.9.1_64bit_WinZip_Installer.zip

BLD_VCPKG=0
!if "$(LIBRARY_INC)"=="" || "$(LIBRARY_LIB)"==""
BLD_VCPKG=1
LIBRARY_INC=$(VCPKG_DIR)\$(VCPKG_TRIPLET)\include
LIBRARY_LIB=$(VCPKG_DIR)\$(VCPKG_TRIPLET)\lib
!endif

BLD_CSPICE=0
!if "$(SPICE)"=="yes"
!  if "$(CSPICE_INC)"=="" || "$(CSPICE_LIB)"==""
BLD_CSPICE=1
CSPICE_INC=$(BD)\cspice\include
CSPICE_LIB=$(BD)\cspice\lib\cspice.lib
!  endif
!endif

BLD_CDF=0
!if "$(CDF)"=="yes"
!  if "$(CDF_INC)"=="" || "$(CDF_LIB)"==""
BLD_CDF=1
CDF_INC=$(BD)\cdf\include
CDF_LIB=$(BD)\cdf\lib\libcdf.lib
!  endif
!endif

# The expat library name depends on the packager and version
ED=$(LIBRARY_LIB)
!if exist($(ED)\libexpatMT.lib)
EXPAT_LIB=$(ED)\libexpatMT.lib
!elseif exist($(ED)\libexpatMD.lib)
EXPAT_LIB=$(ED)\libexpatMD.lib
!else
EXPAT_LIB=$(ED)\libexpat.lib
!endif

# Static zlib is zs.lib in newer vcpkg releases
!if exist($(ED)\zs.lib)
ZLIB_LIB=$(ED)\zs.lib
!else
ZLIB_LIB=$(ED)\zlib.lib
!endif

EX_LIBS=$(EXPAT_LIB) $(ED)\fftw3.lib $(ZLIB_LIB) $(ED)\libssl.lib \
 $(ED)\libcrypto.lib $(ED)\pthreadVC3.lib Advapi32.lib User32.lib Crypt32.lib \
 ws2_32.lib

##############################################################################
# Build definitions

# Optimization and debug info, override on the command line if needed
OPTFLAGS=/O2 /Z7

# WISDOM_FILE is where das3\dft.c looks for this computer's fftw wisdom
CFLAGS=$(CFLAGS) $(OPTFLAGS) /DWISDOM_FILE=C:/ProgramData/fftw3/wisdom.dat \
 /I . /I $(LIBRARY_INC)
# /DEBUG turns off removal of unused code unless /OPT:REF asks for it back
LFLAGS=/link /DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF

_SRCS=$(LIB_SRCS)
_HDRS=$(LIB_HDRS)
_USRCS=$(UTIL_SRCS)
_TSRCS=$(TEST_SRCS)

!if "$(SPICE)"=="yes"
CFLAGS=$(CFLAGS) /I $(CSPICE_INC)
EX_LIBS=$(EX_LIBS) $(CSPICE_LIB)
_SRCS=$(_SRCS) $(SPICE_LIB_SRCS)
_HDRS=$(_HDRS) $(SPICE_LIB_HDRS)
_USRCS=$(_USRCS) $(SPICE_UTIL_SRCS)
_TSRCS=$(_TSRCS) $(SPICE_TEST_SRCS)
!endif

!if "$(CDF)"=="yes"
_USRCS=$(_USRCS) $(CDF_UTIL_SRCS)
!endif

# Derived lists, see buildfiles\sources.mak
_OBJS=$(_SRCS:.c=.obj)
STATIC_OBJS=$(_OBJS:das3/=build.windows\static\)
DLL_OBJS=$(_OBJS:das3/=build.windows\shared\)
HDRS=$(_HDRS:/=\)
_UEXES=$(_USRCS:.c=.exe)
UTIL_PROGS=$(_UEXES:utilities/=build.windows\)
_TEXES=$(_TSRCS:.c=.exe)
TEST_PROGS=$(_TEXES:test/=build.windows\)

##############################################################################
# Public targets

build: build_dep
	$(MAKE) /f $(THIS) static progs

# Not part of build or install, ask for it by name
dll: build_dep
	$(MAKE) /f $(THIS) $(BD)\$(TARG).dll

TEST_SETS=test_main
!if "$(SPICE)"=="yes"
TEST_SETS=$(TEST_SETS) test_spice
!endif
!if "$(CDF)"=="yes"
TEST_SETS=$(TEST_SETS) test_cdf
!endif

test: build
	$(MAKE) /f $(THIS) $(TEST_SETS)

##############################################################################
# Dependencies

_DEPS=
!if $(BLD_VCPKG)==1
_DEPS=$(_DEPS) $(VCPKG_STAMP)
!endif
!if $(BLD_CSPICE)==1
_DEPS=$(_DEPS) $(CSPICE_LIB)
!endif
!if $(BLD_CDF)==1
_DEPS=$(_DEPS) $(CDF_LIB)
!endif

build_dep: $(_DEPS)

$(VCPKG_STAMP): vcpkg.json
	@if "$(VCPKG_ROOT)"=="" (echo ERROR: Set VCPKG_ROOT to a vcpkg clone, or set LIBRARY_INC and LIBRARY_LIB & exit 3)
	"$(VCPKG_ROOT)\vcpkg.exe" install --triplet $(VCPKG_TRIPLET) --x-install-root=$(VCPKG_DIR)
	echo Installed from vcpkg.json > $@

$(BD)\cspice\lib\cspice.lib:
	@if not exist $(BD) mkdir $(BD)
	$(CURL) -o $(BD)\cspice.zip $(LOC_CSPICE_URL)
	$(TAR) -x -f $(BD)\cspice.zip -C $(BD) cspice/include cspice/lib/cspice.lib
	del $(BD)\cspice.zip

$(BD)\cdf\lib\libcdf.lib:
	@if not exist $(BD)\cdf mkdir $(BD)\cdf
	$(CURL) -o $(BD)\cdf.zip $(LOC_CDF_URL)
	$(TAR) -x -f $(BD)\cdf.zip -C $(BD)\cdf include lib/libcdf.lib
	del $(BD)\cdf.zip

##############################################################################
# Library and programs, these expect build_dep to have run

static: $(BD)\lib$(TARG).lib

progs: $(TEST_PROGS) $(UTIL_PROGS)

$(BD)\lib$(TARG).lib:$(STATIC_OBJS)
	lib /nologo $(STATIC_OBJS) /out:$@

$(BD)\$(TARG).dll:$(DLL_OBJS)
	link /nologo /dll /DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF $(DLL_OBJS) $(EX_LIBS) /out:$@ /implib:$(BD)\$(TARG).lib

# nmake can't track which headers each file includes, so rebuild everything
# when any header changes.  Programs are compiled and linked in one step, so
# they rebuild along with the library.
$(STATIC_OBJS) $(DLL_OBJS) $(BD)\cdfmodel.obj: $(SD)\*.h

$(UTIL_PROGS) $(TEST_PROGS): $(BD)\lib$(TARG).lib

# Batch mode inference rules for the library, one cl call per batch
{$(SD)\}.c{$(LD)\}.obj::
	@if not exist $(LD) mkdir $(LD)
	$(CC) $(CFLAGS) /MP /Fo$(LD)\ /c $<

{$(SD)\}.c{$(DD)\}.obj::
	@if not exist $(DD) mkdir $(DD)
	$(CC) $(CFLAGS) /MP /DDAS_USE_DLL /DBUILDING_DLL /Fo$(DD)\ /c $<

# Single file test and utility programs
{test\}.c{$(BD)\}.exe:
	$(CC) $(CFLAGS) /Fo$(BD)\ /Fe$@ $< $(BD)\lib$(TARG).lib $(EX_LIBS) $(LFLAGS)

{utilities\}.c{$(BD)\}.exe:
	$(CC) $(CFLAGS) /Fo$(BD)\ /Fe$@ $< $(BD)\lib$(TARG).lib $(EX_LIBS) $(LFLAGS)

# Programs that need more than one source file
$(BD)\das2_bin_ratesec.exe:utilities\das2_bin_ratesec.c utilities\via.c
	$(CC) $(CFLAGS) /Fo$(BD)\ /Fe$@ utilities\das2_bin_ratesec.c utilities\via.c \
	 $(BD)\lib$(TARG).lib $(EX_LIBS) $(LFLAGS)

# CDF programs
$(BD)\das3_cdf.exe:utilities\das3_cdf.c
	$(CC) $(CFLAGS) /I $(CDF_INC) /Fo$(BD)\ /Fe$@ utilities\das3_cdf.c \
	 $(BD)\lib$(TARG).lib $(CDF_LIB) $(EX_LIBS) $(LFLAGS)

# The CDF file model is shared by the das?_from_cdf programs
$(BD)\cdfmodel.obj:utilities\cdfmodel.c utilities\cdfmodel.h
	$(CC) $(CFLAGS) /I $(CDF_INC) /Fo$@ /c utilities\cdfmodel.c

$(BD)\das3_from_cdf.exe:utilities\das3_from_cdf.c utilities\cdfmodel.h $(BD)\cdfmodel.obj
	$(CC) $(CFLAGS) /I $(CDF_INC) /Fo$(BD)\ /Fe$@ utilities\das3_from_cdf.c \
	 $(BD)\cdfmodel.obj $(BD)\lib$(TARG).lib $(CDF_LIB) $(EX_LIBS) $(LFLAGS)

$(BD)\das2_from_cdf.exe:utilities\das2_from_cdf.c utilities\cdfmodel.h $(BD)\cdfmodel.obj
	$(CC) $(CFLAGS) /I $(CDF_INC) /Fo$(BD)\ /Fe$@ utilities\das2_from_cdf.c \
	 $(BD)\cdfmodel.obj $(BD)\lib$(TARG).lib $(CDF_LIB) $(EX_LIBS) $(LFLAGS)

# TestCdf reads CDFs back with the CDF library alone, so it links like das3_cdf
$(BD)\TestCdf.exe:test\TestCdf.c $(BD)\lib$(TARG).lib
	$(CC) $(CFLAGS) /I $(CDF_INC) /Fo$(BD)\ /Fe$@ test\TestCdf.c \
	 $(BD)\lib$(TARG).lib $(CDF_LIB) $(EX_LIBS) $(LFLAGS)

##############################################################################
# Tests, these expect the programs to be built, use the 'test' target

# Stream fixtures for TestV3Read and TestFeed.  Linux.mak finds these with
# wildcards, nmake can't, so add new fixtures here by hand.  Left out on
# purpose, as in Linux.mak: examples\ex28_*, test\streams\notimp_* and reject_*
V2_FIXTURES = examples\ex01_polar_mfe.d2s examples\ex01_polar_mfe.d2t \
 examples\ex02_galileo_sys3.d2t examples\ex03_galileo_pws_lrs.d2t \
 examples\ex04_voyager_pws_sa.d2s examples\ex04_voyager_pws_sa.d2t \
 examples\ex05_vgr_pws_sa_peaks.d2s examples\ex05_vgr_pws_sa_peaks.d2t \
 examples\ex06_cassini_rpws_redef.d2s examples\ex06_cassini_rpws_redef.d2t \
 examples\ex07_cassini_rpws_wbr.d2s examples\ex08_cassini_rpws_survey.d2t \
 examples\ex09_juno_waves_survey.avg60.d2t examples\ex09_juno_waves_survey.d2t \
 examples\ex10_juno_waves_hfrh.d2t examples\ex10_juno_waves_hfrh_histo.d2t \
 examples\ex11_juno_fgm_scse.d2s examples\ex11_juno_fgm_scse.d2t \
 examples\ex13_marsis_bmag.d2t test\streams\das2_swap_test.d2s \
 test\streams\das2_swap_test.d2t

V3_FIXTURES = examples\ex11_juno_fgm_scse.d3t examples\ex12_sounder_xyz.d3t \
 examples\ex14_var_len_item.d3t examples\ex15_vector_frame.d3t \
 examples\ex17_vector_noframe.d3b examples\ex18_juno_waves_wfrm.d3b \
 examples\ex19_cassini_ragged_wfrm.d3t examples\ex20_tracers_aci.d3t \
 examples\ex21_tracers_cdpu_status.d3b examples\ex22_mag_grid_vec.d3b \
 examples\ex22_mag_grid_vec.d3t examples\ex23_tracers_mag_hsk.d3b \
 examples\ex24_isee_rapid_rank1.d3b examples\ex24_isee_rapid_rank1.d3t \
 examples\ex25_isee_rapid_rank2.d3b examples\ex25_isee_rapid_rank2.d3t \
 examples\ex26_isee_rapid_rank3.d3b examples\ex26_isee_rapid_rank3.d3t \
 examples\ex27_epop_fai_mgf_blob.d3b examples\ex27_epop_fai_mgf_blob.d3t \
 examples\ex29_ext_contract.d3t examples\ex29_ext_contract_flat.d3t \
 examples\ex30_cassini_ragged_notlast.d3b \
 examples\ex30_cassini_ragged_notlast.d3t examples\ex31_efi_ragged_vec.d3b \
 examples\ex31_efi_ragged_vec.d3t examples\ex32_marsis_2d_ragged.d3b \
 examples\ex32_marsis_2d_ragged.d3t examples\ex33_cassini_ragged_utf8.d3b \
 examples\ex33_cassini_ragged_utf8.d3t examples\ex34_ragged_fixstr.d3b \
 examples\ex34_ragged_fixstr.d3t examples\ex35_strings_rank2.d3b \
 examples\ex35_strings_rank2.d3t examples\ex36_events_rank3.d3b \
 examples\ex36_events_rank3.d3t examples\ex37_wide_fixed_utf8.d3b \
 examples\ex37_wide_fixed_utf8.d3t examples\ex38_wbr_wfrm_tags.d3b \
 examples\ex38_wbr_wfrm_tags.d3t examples\ex39_sandwich.d3b \
 examples\ex39_sandwich.d3t examples\ex40_rotation.d3b \
 examples\ex40_rotation.d3t examples\ex41_quaternion.d3b \
 examples\ex41_quaternion.d3t examples\ex42_plain_tensor.d3b \
 examples\ex42_plain_tensor.d3t examples\ex43_msc_complex_cal.d3b \
 examples\ex43_msc_complex_cal.d3t examples\ex44_time_sequence.d3t \
 test\streams\das3_test_props.d3t

# The golden-file comparisons at the end of test_main in Linux.mak are shell
# scripts and don't run here yet.
test_main:
	@echo INFO: --- variable layer first: fastest, highest value, no goldens ---
	@echo INFO: Running unit test for the value layer, $(BD)\TestValue...
	@$(BD)\TestValue.exe
	@echo INFO: Running unit test for the datum layer, $(BD)\TestDatum...
	@$(BD)\TestDatum.exe
	@echo INFO: Running unit test for the DasGen value sources, $(BD)\TestGen...
	@$(BD)\TestGen.exe
	@echo INFO: Running unit test for the DasForm formalisms, $(BD)\TestForm...
	@$(BD)\TestForm.exe
	@echo INFO: Running unit test for complex arithmetic, $(BD)\TestCplx...
	@$(BD)\TestCplx.exe
	@echo INFO: Running unit test for the DasVar layer, $(BD)\TestVar...
	@$(BD)\TestVar.exe
	@echo INFO: Running unit test for DasVar bulk reads, $(BD)\TestVarSubset...
	@$(BD)\TestVarSubset.exe
	@echo INFO: Running unit test for the DasDim container, $(BD)\TestDim...
	@$(BD)\TestDim.exe
	@echo INFO: Running unit test to test units, $(BD)\TestUnits...
	@$(BD)\TestUnits.exe
	@echo INFO: Running unit test for TT2000 leap seconds, $(BD)\TestTT2000...
	@$(BD)\TestTT2000.exe
	@echo INFO: Running unit test for dynamic arrays, $(BD)\TestArray...
	@$(BD)\TestArray.exe
	@echo INFO: --- datasets, streams and iteration ---
	@echo INFO: Running unit test for dataset shape/length merge, $(BD)\TestDs...
	@$(BD)\TestDs.exe
	@echo INFO: Running unit test for dataset builder, $(BD)\TestBuilder...
	@$(BD)\TestBuilder.exe
	@echo INFO: Running unit test for dataset loader, $(BD)\das3_info...
	@$(BD)\das3_info.exe -q examples\ex07_cassini_rpws_wbr.d2s
	@echo INFO: Running unit test for stream parsing over all fixtures, $(BD)\TestV3Read...
	@$(BD)\TestV3Read.exe test\streams\tag_test.dNt $(V2_FIXTURES) $(V3_FIXTURES)
	@echo INFO: Running unit test for push parsing vs. pull parsing, $(BD)\TestFeed...
	@$(BD)\TestFeed.exe test\streams\tag_test.dNt $(V2_FIXTURES) $(V3_FIXTURES)
	@echo INFO: Running unit test for ragged and unique iteration, $(BD)\TestIter...
	$(BD)\TestIter.exe
	@echo INFO: Running unit test for ragged binary re-encode, $(BD)\TestRaggedEncode...
	$(BD)\TestRaggedEncode.exe examples\ex30_cassini_ragged_notlast.d3b \
	 examples\ex31_efi_ragged_vec.d3b examples\ex32_marsis_2d_ragged.d3b \
	 examples\ex34_ragged_fixstr.d3b examples\ex38_wbr_wfrm_tags.d3b \
	 examples\ex39_sandwich.d3b
	@echo INFO: Running unit test for filter dataset ops (copy/swap), $(BD)\TestFilter...
	$(BD)\TestFilter.exe
	$(BD)\TestUri.exe $(BD)\uri_tplt
	@echo INFO: Running unit test for credentials manager, $(BD)\TestCredMngr...
	@$(BD)\TestCredMngr.exe $(BD)
	@echo INFO: Running unit test for exception names and send.h writers, $(BD)\TestOob...
	@$(BD)\TestOob.exe
	$(BD)\TestOob.exe send 2 > $(BD)\oob_send.d2s
	$(BD)\TestOob.exe read $(BD)\oob_send.d2s
	$(BD)\TestOob.exe send 3 > $(BD)\oob_send.d3t
	$(BD)\TestOob.exe read $(BD)\oob_send.d3t
	@echo INFO: Running unit test for v2.3.0 calls via das2_compat.h, $(BD)\TestCompat...
	@$(BD)\TestCompat.exe $(BD)
	@echo INFO: Running unit test for CSVs with variable length item data, $(BD)\das3_csv
	$(BD)\das3_csv.exe < examples\ex21_tracers_cdpu_status.d3b > $(BD)\ex21_tracers_cdpu_status.csv
	@echo INFO: ===============================================
	@echo INFO: All core test programs completed without errors
	@echo INFO: ===============================================

test_spice:
	@echo INFO: Running unit test for spice error redirect, $(BD)\TestSpice...
	@$(BD)\TestSpice.exe

# Can't test CDF creation by comparison due to the embedded time stamps
#
# Known exclusions from the das3_cdf fixture set:
#   ex22: a rank 3 grid with coordinates that vary along the grid indices only;
#         no ISTP DEPEND layout fits it, a bespoke converter is the answer.
#   ex27: carries a byte run (blob), which das3_cdf does not write yet.
#
test_cdf: $(BD)\TestCdf.exe
	@echo INFO: Testing CDF creation
	$(BD)\das3_cdf.exe -l warning -i examples\ex12_sounder_xyz.d3t -o $(BD) -r
	@echo INFO: CDF was created
	@echo INFO: Testing composite variables in CDF output (labels, units, C-order flattening)
	$(BD)\das3_cdf.exe -l warning -i examples\ex15_vector_frame.d3t -o $(BD) -r
	$(BD)\das3_cdf.exe -l warning -i examples\ex15_vector_frame.d3t -o $(BD)\ex15_mapped.cdf -r -p FRAME_NAME:COORD_FRAME
	@echo INFO: Testing das extension attributes and a CDF round trip (ex20)
	$(BD)\das3_cdf.exe -l warning -i examples\ex20_tracers_aci.d3t -o $(BD) -r
	$(BD)\das3_from_cdf.exe -l warning $(BD)\ex20_tracers_aci.cdf > $(BD)\ex20_rt.d3b
	$(BD)\das3_cdf.exe -l warning -i $(BD)\ex20_rt.d3b -o $(BD)\ex20_rt.cdf -r
	$(BD)\das3_cdf.exe -l warning -i examples\ex40_rotation.d3b -o $(BD) -r
	$(BD)\das3_cdf.exe -l warning -i examples\ex41_quaternion.d3b -o $(BD) -r
	$(BD)\das3_cdf.exe -l warning -i examples\ex42_plain_tensor.d3b -o $(BD) -r
	$(BD)\das3_cdf.exe -l warning -i examples\ex43_msc_complex_cal.d3b -o $(BD) -r
	$(BD)\das3_cdf.exe -l warning -i examples\ex43_msc_complex_cal.d3b -o $(BD)\ex43_keep.cdf -r -k
	$(BD)\TestCdf.exe $(BD)

##############################################################################
# Install

# Under conda-build, install into the environment's Library directory
!if defined(CONDA_BUILD_STATE)
INST_ROOT=$(LIBRARY_PREFIX)
!else
INST_ROOT=$(PREFIX)
!endif

!if "$(INST_NAT_BIN)"==""
INST_NAT_BIN=$(INST_ROOT)\bin
!endif
!if "$(INST_NAT_LIB)"==""
INST_NAT_LIB=$(INST_ROOT)\lib
!endif
!if "$(INST_INC)"==""
INST_INC=$(INST_ROOT)\include
!endif
!if "$(INST_SHARE)"==""
INST_SHARE=$(INST_ROOT)\share
!endif

# Programs and schemas.  With CDF=yes and SPICE=yes UTIL_PROGS already holds
# the CDF and SPICE programs.
install: bin_install

bin_install: _inst_check build
	@if not exist "$(INST_NAT_BIN)" mkdir "$(INST_NAT_BIN)"
	@if not exist "$(INST_SHARE)\das2C" mkdir "$(INST_SHARE)\das2C"
	for %f in ($(UTIL_PROGS)) do @copy /y %f "$(INST_NAT_BIN)" >nul
	copy /y schema\*.xsd "$(INST_SHARE)\das2C" >nul

# What a program needs to compile and link against the library, on top of
# the above
dev_install: bin_install _hdr_install
	@if not exist "$(INST_NAT_LIB)" mkdir "$(INST_NAT_LIB)"
	copy /y $(BD)\lib$(TARG).lib "$(INST_NAT_LIB)" >nul

# The DLL, its import library and the headers.  Not part of install, ask for
# it by name.  The DLL goes in bin since Windows finds DLLs on the PATH.
dll_install: _inst_check dll _hdr_install
	@if not exist "$(INST_NAT_BIN)" mkdir "$(INST_NAT_BIN)"
	@if not exist "$(INST_NAT_LIB)" mkdir "$(INST_NAT_LIB)"
	copy /y $(BD)\$(TARG).dll "$(INST_NAT_BIN)" >nul
	copy /y $(BD)\$(TARG).lib "$(INST_NAT_LIB)" >nul

_hdr_install: _inst_check
	@if not exist "$(INST_INC)\das3" mkdir "$(INST_INC)\das3"
	for %f in ($(HDRS)) do @copy /y %f "$(INST_INC)\das3" >nul

_inst_check:
	@if "$(INST_ROOT)"=="" (echo ERROR: Set PREFIX to the install location, e.g. nmake /f $(THIS) PREFIX=C:\opt install & exit 3)

##############################################################################
# Cleanup, clean keeps the downloaded and vcpkg built dependencies

clean:
	-del /q $(BD)\*.exe $(BD)\*.obj $(BD)\*.pdb $(BD)\*.ilk $(BD)\*.lib \
	 $(BD)\*.dll $(BD)\*.exp $(BD)\ex* $(BD)\cred_test* 2>nul
	-if exist $(LD) rmdir /s /q $(LD)
	-if exist $(DD) rmdir /s /q $(DD)

distclean:
	-if exist $(BD) rmdir /s /q $(BD)
