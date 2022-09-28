# Building RPMs

First setup your build environment, including an rpmbuild tree in your home 
directory:
```bash
$ yum install gcc rpm-build rpm-devel rpmlint make python bash coreutils diffutils patch rpmdevtools
$ rpmdev-setuptree
```

Copy the included spec and patch files to locations within your rpmbuild tree.  The
destdir patch is needed because version `v2.3-pre4` did not have the DESTDIR macro
and thus the install targets were not relocatable for two-stage installs.  Future releases (aka v2.3-pre5, etc.) will not need this file.
```bash
cp makefiles/rpm/das2C.spec $HOME/rpmbuild/SPECS/
cp makefiles/rpm/das2C-destdir.patch $HOME/rpmbuild/SOURCES/
```

Install dependencies as usual:
```bash
yum install expat-devel fftw-devel openssl-devel
```

Build the RPMs and the SRPM:
```bash
$ rpmbuild -bs $HOME/rpmbuild/SPECS/das2C.spec  # Source RPM
$ rpmbuild -bb $HOME/rpmbuild/SPECS/das2C.spec  # lib, devel & debug RPMs
```

Install the binary RPMs
```bash
$ sudo yum localinstall $HOME/rpmbuild/RPMS/x86_64/das2C*.rpm
```

Test:
```bash
$ das2_psd -h
$ das
$ cat test/cassini_rpws_wfrm_sample.d2s | das2_psd 512 1 | das2_bin_avgsec 1 | das2_ascii -s 3 -r 3
```
Since the input file is only a second's worth of data, this should produce a 
UTF-8 stream with only a single record with 257 items (i.e. 512/2 + 1).
