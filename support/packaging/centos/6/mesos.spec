%global MESOS_VERSION
%global MESOS_RELEASE

Name:          mesos
Version:       %{MESOS_VERSION}
Release:       %{MESOS_RELEASE}%{?dist}
Summary:       Cluster manager for sharing distributed application frameworks
License:       ASL 2.0
URL:           http://mesos.apache.org/

ExclusiveArch: x86_64

Source0:       %{name}-%{version}.tar.gz
Source1:       mesos-init-wrapper
Source2:       %{name}-master.upstart
Source3:       %{name}-slave.upstart
Source4:       %{name}
Source5:       %{name}-master
Source6:       %{name}-slave

BuildRequires: libtool
BuildRequires: automake
BuildRequires: autoconf
BuildRequires: java-devel
BuildRequires: zlib-devel
BuildRequires: libcurl-devel
BuildRequires: openssl-devel
BuildRequires: cyrus-sasl-devel
BuildRequires: cyrus-sasl-md5

BuildRequires: apr-devel
BuildRequires: subversion-devel

Requires: cyrus-sasl-md5
Requires: docker

# The slaves will indirectly require time syncing with the master
# nodes so just call out the dependency.
Requires: ntpdate

%description
Apache Mesos is a cluster manager that provides efficient resource
isolation and sharing across distributed applications, or frameworks.
It can run Hadoop, MPI, Hypertable, Spark, and other applications on
a dynamically shared pool of nodes.

%prep
%setup -q

%build
%configure --enable-optimize \
    --disable-python-dependency-install \
    --enable-optimize \
    --enable-install-module-dependencies \
    --enable-libevent \
    --enable-ssl

make %{?_smp_mflags} V=0

%check

%install
%make_install

mkdir -p -m0755 %{buildroot}%{_sysconfdir}/default
mkdir -p -m0755 %{buildroot}%{_sysconfdir}/init
mkdir -p -m0755 %{buildroot}%{_sysconfdir}/%{name}
mkdir -p -m0755 %{buildroot}%{_sysconfdir}/%{name}-master
mkdir -p -m0755 %{buildroot}%{_sysconfdir}/%{name}-slave
mkdir -p -m0755 %{buildroot}%{_unitdir}/
mkdir -p -m0755 %{buildroot}/%{_var}/log/%{name}
mkdir -p -m0755 %{buildroot}/%{_var}/lib/%{name}

echo zk://localhost:2181/mesos > %{buildroot}%{_sysconfdir}/mesos/zk
echo %{_var}/log/%{name}       > %{buildroot}%{_sysconfdir}/mesos-master/work_dir
echo %{_var}/log/%{name}       > %{buildroot}%{_sysconfdir}/mesos-slave/work_dir
echo 1                         > %{buildroot}%{_sysconfdir}/mesos-master/quorum

install -m 0644 %{SOURCE4} %{SOURCE5} %{SOURCE6} %{buildroot}%{_sysconfdir}/default

install -m 0644 %{SOURCE1} %{buildroot}%{_bindir}/
install -m 0644 %{SOURCE2} %{SOURCE3} %{buildroot}%{_unitdir}/

mkdir -p -m0755 %{buildroot}%{_datadir}/java
install -m 0644 src/java/target/mesos-*.jar %{buildroot}%{_datadir}/java/

install -m 0644 %{SOURCE2} %{buildroot}%{_sysconfdir}/init/mesos-master.conf
install -m 0644 %{SOURCE3} %{buildroot}%{_sysconfdir}/init/mesos-slave.conf

%files
%doc LICENSE NOTICE
%{_libdir}/*.so
%{_libdir}/mesos/modules/*.so
%{_bindir}/mesos*
%{_sbindir}/mesos-*
%{_datadir}/%{name}/
%{_libexecdir}/%{name}/
%{python_sitelib}/%{name}*
%attr(0755,mesos,mesos) %{_var}/log/%{name}/
%attr(0755,mesos,mesos) %{_var}/lib/%{name}/
%config(noreplace) %{_sysconfdir}/%{name}*
%config(noreplace) %{_sysconfdir}/default/%{name}*
%config(noreplace) %{_sysconfdir}/init/%{name}-*

######################
#%files devel
%{_includedir}/mesos/
%{_includedir}/stout/
%{_includedir}/process/
%{_includedir}/elfio/
%{_includedir}/picojson.h
%{_libdir}/pkgconfig/%{name}.pc
%{_libdir}/*.la
%{_libdir}/mesos/modules/*.la
%{_libdir}/mesos/3rdparty/

%{_datadir}/java/%{name}-*.jar

%pre

%post
/sbin/ldconfig

%preun

%postun
/sbin/ldconfig

%changelog
* Mon Jan 16 2017 Kapil Arya <kapil@mesosphere.io> - 1.1.0-0.1
- Initial release
