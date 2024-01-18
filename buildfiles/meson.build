project('das2C', 'c',
  version : '3.0.0',
  license : 'LGPL-2.1-only',
  default_options : ['warning_level=2', 'c_std=c99']
)

zlib_dep   = dependency('zlib', fallback : ['zlib', 'zlib_dep'])
expat_dep  = dependency('expat', fallback : ['expat', 'expat_dep'])
fftw3_dep  = dependency('fftw3', fallback : ['fftw3','fftw3_dep'])
ssl_dep    = dependency('openssl', fallback : ['openssl','ssl_dep'])
thread_dep = dependency('threads')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)

if cc.get_id() == 'gcc' or cc.get_id() == 'clang'
	extra_warn_args = [
		# Yes, we do have function pointers, and signatures have to match, get  
		# GCC to let this one go
		'-Wno-unused-parameter',
		
		# These are carefully done in dataset.c and friends, so the warning 
		# should be disable in those source files only, not sure how 
		'-Wno-sign-compare',
		
		# When did this become a problem?  Much of the code intentionally 
		# depends on fields being automatically assigned to 0
		'-Wno-missing-field-initializers',
		
		# I also depend on format truncation in snprintf, that's why I'm using
		# s *N* printf, seriously GCC, chill out.
		'-Wno-format-truncation'
	]
else
	extra_warn_args = []
endif

add_project_arguments(extra_warn_args, language : 'c')

#inc = include_directories('.')

install_headers(
   'das2/array.h',      'das2/buffer.h',      'das2/builder.h',
   'das2/core.h',       'das2/credentials.h', 'das2/das1.h',
   'das2/dataset.h',    'das2/datum.h',       'das2/defs.h',
   'das2/descriptor.h', 'das2/dft.h',         'das2/dimension.h',
   'das2/dsdf.h',       'das2/encoding.h',    'das2/http.h',
   'das2/io.h',         'das2/json.h',        'das2/log.h',
   'das2/node.h',       'das2/oob.h',         'das2/operator.h',
   'das2/packet.h',     'das2/plane.h',       'das2/processor.h',
   'das2/stream.h',     'das2/time.h',        'das2/tt2000.h',
   'das2/units.h',      'das2/utf8.h',        'das2/util.h',
   'das2/value.h',      'das2/variable.h',    
   subdir : 'das2'
)

src_lib = [ 
   'das2/array.c',       'das2/buffer.c',     'das2/builder.c',
   'das2/credentials.c', 'das2/das1.c',       'das2/dataset.c',
   'das2/datum.c',       'das2/descriptor.c', 'das2/dft.c',
   'das2/dimension.c',   'das2/dsdf.c',       'das2/encoding.c',
   'das2/http.c',        'das2/io.c',         'das2/json.c',
   'das2/log.c',         'das2/node.c',       'das2/oob.c',
   'das2/operator.c',    'das2/packet.c',     'das2/plane.c',
   'das2/processor.c',   'das2/stream.c',     'das2/time.c',
   'das2/tt2000.c',      'das2/units.c',      'das2/utf8.c',
   'das2/util.c',        'das2/value.c',      'das2/variable.c'
]

lib_das2 = library(
   'das2', src_lib, #include_directories : inc,  
   install : true,
   dependencies : [
      fftw3_dep, expat_dep, ssl_dep, zlib_dep, m_dep, thread_dep
   ],
   c_args : '-DWISDOM_FILE=/etc/fftw/wisdom'
)

exe_inctime = executable(
   'das1_inctime', 'utilities/das1_inctime.c', link_with : lib_das2, install : true
)
exe_prtime = executable(
   'das2_prtime', 'utilities/das2_prtime.c', link_with : lib_das2, 
   dependencies : dep_m, install : true
)
exe_fxtime = executable(
   'das1_fxtime', 'utilities/das1_fxtime.c', link_with : lib_das2, install : true
)
exe_ascii = executable(
   'das2_ascii', 'utilities/das2_ascii.c', link_with : lib_das2, install : true
)
exe_bin_avg = executable(
   'das2_bin_avg', 'utilities/das2_bin_avg.c', link_with : lib_das2, install : true
)
exe_bin_avgsec = executable(
   'das2_bin_avgsec', 'utilities/das2_bin_avgsec.c', link_with : lib_das2, install : true
)
exe_bin_peakavgsec = executable(
   'das2_bin_peakavgsec', 'utilities/das2_bin_peakavgsec.c', link_with : lib_das2, install : true
)
exe_from_das1 = executable(
   'das2_from_das1', 'utilities/das2_from_das1.c', link_with : lib_das2, install : true
)
exe_from_tagged_das1 = executable(
   'das2_from_tagged_das1', 'utilities/das2_from_tagged_das1.c', link_with : lib_das2, install : true
)
exe_ascii = executable(
   'das1_ascii', 'utilities/das1_ascii.c', link_with : lib_das2, install : true
)
exe_bin_avg = executable(
   'das1_bin_avg', 'utilities/das1_bin_avg.c', link_with : lib_das2, install : true
)
exe_bin_ratesec = executable(
   'das2_bin_ratesec', 'utilities/das2_bin_ratesec.c', 'utilities/via.c', 
   link_with : lib_das2, install : true
)
exe_psd = executable(
   'das2_psd', 'utilities/das2_psd.c', 'utilities/send.c', link_with : lib_das2, install : true
)
exe_hapi = executable(
   'das2_hapi', 'utilities/das2_hapi.c', link_with : lib_das2, install : true
)
exe_histo = executable(
   'das2_histo', 'utilities/das2_histo.c', link_with : lib_das2, install : true
)
exe_cache_rdr = executable(
   'das2_cache_rdr', 'utilities/das2_cache_rdr.c', link_with : lib_das2, 
   dependencies : dep_m, install : true
)

# The test executables
exe_test_arr  = executable('TestArray', 'test/TestArray.c',  link_with : lib_das2)
exe_test_auth = executable('TestAuth', 'test/TestAuth.c', link_with : lib_das2)
exe_test_bldr = executable('TestBuilder','test/TestBuilder.c', link_with : lib_das2)
exe_test_cat  = executable('TestCatalog','test/TestCatalog.c', link_with : lib_das2)
exe_test_tt2k = executable('TestTT2000','test/TestTT2000.c', link_with : lib_das2)
exe_test_unit = executable('TestUnits', 'test/TestUnits.c', link_with : lib_das2)
exe_test_vars = executable('TestVariable','test/TestVariable.c', link_with : lib_das2)

test('Ragged N-D Arrays', exe_test_arr)
test('Authentication', exe_test_auth)
test('Dataset Builder', exe_test_bldr)
test('Federated Catalog', exe_test_cat)
test('Leapseconds', exe_test_tt2k)
test('SI Units Manipulation', exe_test_unit)
test('Index Mapping Variables', exe_test_vars)

pkg_mod = import('pkgconfig')
pkg_mod.generate(
   libraries : lib_das2, version : '3.0', name : 'libdas2', filebase : 'das2',
   description : 'Space physics data stream utilities'
)
