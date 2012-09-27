%define name sliver-openvswitch
# to check for any change:
# grep AC_INIT configure.ac 
%define version 1.8.90
%define taglevel 2

%define debug_package %{nil}

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
# let's be as close as the regular linux/fedora layout
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var
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
* Thu Sep 27 2012 Thierry Parmentelat <thierry.parmentelat@sophia.inria.fr> - sliver-openvswitch-1.8.90-2
- add/skip packet information on tap send/recv

* Wed Sep 26 2012 Thierry Parmentelat <thierry.parmentelat@sophia.inria.fr> - sliver-openvswitch-1.8.90-1
- merged mainstream 1.8.90
- planetlab extensions to the openvswitch: single helper command tool 'sliver-ovs' in /usr/sbin
- planetlab exp-tool : single config file (conf.mk)
- planetlab exp-tool : can retrieve and save current topology
