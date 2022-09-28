project('fftw3 windows binaries', 'c')

cc = meson.get_compiler('c')
fftw3_dep = declare_dependency(
  dependencies : cc.find_library('fftw3', dirs : meson.current_source_dir()),
  include_directories :  meson.current_source_dir()))
)
