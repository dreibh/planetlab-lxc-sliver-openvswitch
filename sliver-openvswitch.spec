%define name sliver-openvswitch
%define version 0.1
%define taglevel 1

%define release %{taglevel}%{?pldistro:.%{pldistro}}%{?date:.%{date}}

Vendor: OneLab
Packager: OneLab <support@planet-lab.eu>
Distribution: PlanetLab %{plrelease}
URL: %{SCMURL}
#Requires: 

Summary: Openvswitch modified for running from a PlanetLab sliver
Name: %{name}
Version: %{version}
Release: %{release}
License: GPL
Group: System Environment/Applications
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
Source0: sliver-openvswitch-%{version}.tar.gz

%description
Openvswitch tuned for running within a PlanetLab sliver

%prep 
%setup -q

%build
./boot.sh
./configure --prefix=/usr
make

%install
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
/usr

%post

%postun

%changelog
