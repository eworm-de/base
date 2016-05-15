Name:           org.bus1.base
Version:        1
Release:        3
Summary:        Bus1 Base Services
License:        LGPL2+
URL:            https://github.com/bus1/base
Source0:        %{name}.tar.xz
BuildRequires:  autoconf automake pkgconfig
BuildRequires:  c-sundry-devel
BuildRequires:  elfutils-devel
BuildRequires:  kmod-devel
BuildRequires:  libbus1-devel
BuildRequires:  libcap-devel
BuildRequires:  openssl-devel

%description
Bus1 Base Services and Tools

%package        devel
Summary:        Development files for %{name}
Requires:       %{name} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q

%build
./autogen.sh
%configure
make %{?_smp_mflags}

%install
%make_install

%files
%doc COPYING
%{_bindir}/org.bus1.*

%files devel
%{_includedir}/org.bus1/*.h

%changelog
* Sun May 15 2016 <kay@redhat.com> 1-4
- use c-sundry

* Fri Apr 29 2016 <kay@redhat.com> 1-3
- rebuild for libbus1 update

* Tue Apr 26 2016 <kay@redhat.com> 1-2
- rebuild for libbus1 update

* Tue Apr 26 2016 <kay@redhat.com> 1-1
- org.bus1.base 1
