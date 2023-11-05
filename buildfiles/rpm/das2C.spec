Name:           das2C
Version:        2.3
%global tagver  2.3

Release:        1%{?dist}
Summary:        das2 stream utilities and catalog client in C

Group:          System Environment/Libraries
License:        LGPLv2
URL:            https://github.com/das-developers/%{name}

# Download the source from github automatically, normally distro maintainers
# can't do this because they have to verify source integrety.  For custom
# build RPMs of local projects this is probably okay.
%undefine _disable_source_fetch
Source0:        https://github.com/das-developers/%{name}/archive/refs/tags/v%{tagver}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

# Patch the makefile so that we can do staged installs using the DESTDIR
# variable
Patch0:         %{name}-destdir.patch

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  expat-devel
BuildRequires:  fftw-devel
BuildRequires:  openssl-devel
BuildRequires:  zlib-devel

Requires: expat
Requires: fftw-libs
Requires: openssl-libs
Requires: zlib

%description
The das2C package supports generation and processing of das2 data streams.
It is also a das2 catalog client and handles dynamic source ID to URL mapping.
This package contains userspace libraries and programs.

%package       devel
Summary:       Development files for %{name}
Group:         evelopment/Libraries
Requires:      %{name}%{?_isa} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q -n das2C-%{tagver}
%patch0 

%build
make %{?_smp_mflags} PREFIX=%{_prefix} N_ARCH=/ INST_NAT_LIB=%{_libdir} INST_INC=%{_includedir}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT PREFIX=%{_prefix} N_ARCH=/ INST_NAT_LIB=%{_libdir} INST_INC=%{_includedir}
find $RPM_BUILD_ROOT -name '*.la' -exec rm -f {} ';'


%clean
rm -rf $RPM_BUILD_ROOT


%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig


%files
%defattr(-,root,root,-)
%doc
%{_bindir}/das1_ascii
%{_bindir}/das1_bin_avg
%{_bindir}/das1_fxtime
%{_bindir}/das1_inctime
%{_bindir}/das2_ascii
%{_bindir}/das2_bin_avg
%{_bindir}/das2_bin_avgsec
%{_bindir}/das2_bin_peakavgsec
%{_bindir}/das2_bin_ratesec
%{_bindir}/das2_cache_rdr
%{_bindir}/das2_from_das1
%{_bindir}/das2_from_tagged_das1
%{_bindir}/das2_hapi
%{_bindir}/das2_histo
%{_bindir}/das2_prtime
%{_bindir}/das2_psd
%{_libdir}/*.so

%files devel
%defattr(-,root,root,-)
%{_includedir}/das2/*
%{_libdir}/*.a


%changelog
* Sun Nov 28 2021 Chris Piker <chris-piker@uiowa.edu> - 2.3-pre4
- First das2C package
