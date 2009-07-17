#
# rpm spec for xtrabackup
#
%{!?redhat_version:%define redhat_version 5}
%define distribution  rhel%{redhat_version}
%define release       1.%{distribution}
%{!?xtrabackup_version:%define xtrabackup_version undefined}
%{!?xtrabackup_revision:%define xtrabackup_revision undefined}

Summary: XtraBackup online backup for MySQL / InnoDB 
Name: xtrabackup
Version: %{xtrabackup_version}
Release: %{release}
Group: Server/Databases
License: GPLv2
Packager: Vadim Tkachenko <vadim@percona.com>
URL: http://percona.com/percona-lab.html
Source: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-root

%description
Percona XtraBackup is OpenSource online (non-blockable) backup solution for InnoDB and XtraDB engines.


%changelog
* Fri Mar 13 2009 Vadim Tkachenko
- initial release


%prep
%setup -q
tar zxf $RPM_SOURCE_DIR/libtar-1.2.11.tar.gz
cd libtar-1.2.11
patch -p1 < ../innobase/xtrabackup/tar4ibd_libtar-1.2.11.patch


%build
export CC=${CC-"ccache gcc"} 
export CXX=$CC 
export CFLAGS="$CFLAGS -DXTRABACKUP_VERSION=\\\"%{xtrabackup_version}\\\" -DXTRABACKUP_REVISION=\\\"%{xtrabackup_revision}\\\"" 
./configure \
  --prefix=%{_prefix} --with-extra-charsets=complex
make -j`if [ -f /proc/cpuinfo ] ; then grep -c processor.* /proc/cpuinfo ; else echo 1 ; fi`
cd innobase/xtrabackup
make
cd ../..
cd libtar-1.2.11
./configure --prefix=%{_prefix}
make

%install
[ "%{buildroot}" != '/' ] && rm -rf %{buildroot}
install -d %{buildroot}%{_bindir}
# install binaries and configs
install -m 755 innobase/xtrabackup/{innobackupex-1.5.1,xtrabackup} %{buildroot}%{_bindir}
install -m 755 libtar-1.2.11/libtar/tar4ibd %{buildroot}%{_bindir}

%clean
[ "%{buildroot}" != '/' ] && rm -rf %{buildroot}

%files
%defattr(-,root,root)
%{_bindir}/innobackupex-1.5.1
%{_bindir}/xtrabackup
%{_bindir}/tar4ibd


###
### eof
###


