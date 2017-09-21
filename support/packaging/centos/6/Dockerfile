FROM centos:6
MAINTAINER Kapil Arya <kapil@apache.org>

# Get curl.
RUN yum install -y wget curl unzip

# Add the Extra packages for enterprise linux repo.
RUN yum install -y epel-release

# Add the Subversion repo.
RUN echo -e '[WANdiscoSVN]\n\
name=WANdisco SVN Repo 1.9\n\
enabled=1\n\
baseurl=http://opensource.wandisco.com/centos/6/svn-1.9/RPMS/\$basearch/\n\
gpgcheck=1\n\
gpgkey=http://opensource.wandisco.com/RPM-GPG-KEY-WANdisco' \
>> /tmp/wandisco-svn.repo
RUN mv /tmp/wandisco-svn.repo /etc/yum.repos.d/

# Add Kernel 3.10 repo.
RUN  rpm --import https://www.elrepo.org/RPM-GPG-KEY-elrepo.org
RUN  rpm -Uvh --replacepkgs \
      http://www.elrepo.org/elrepo-release-6-8.el6.elrepo.noarch.rpm

# PostgreSQL repo for libevent2.
RUN  rpm -Uvh --replacepkgs \
      http://yum.postgresql.org/9.5/redhat/rhel-6-x86_64/pgdg-centos95-9.5-2.noarch.rpm

# Add the Apache Maven repo.
RUN curl -sSL \
      http://repos.fedorapeople.org/repos/dchen/apache-maven/epel-apache-maven.repo \
      -o /etc/yum.repos.d/epel-apache-maven.repo

RUN yum makecache

# These packages conflict with Wandisco ones.
RUN yum remove -y subversion subversion-libs

# Update system.
RUN yum update -y

# Update kernel.
RUN yum --enablerepo=elrepo-kernel install -y kernel-lt

# Setup cgroups (we need the perf_event subsystem).
RUN yum install -y libcgroup

#RUN sed -i.old '/mount {/a\
#RUNerf_event = /cgroup/perf_event;
#' /etc/cgconfig.conf

# Install devtoolset3.
RUN yum install -y scl-utils centos-release-scl-rh
RUN yum install -y devtoolset-3-toolchain

# Enable cgroups.
RUN chkconfig cgconfig on

RUN yum groupinstall -y "Development Tools"

RUN yum install -y \
      apache-maven \
      apr-devel \
      apr-util-devel \
      autoconf \
      automake \
      cyrus-sasl-devel \
      cyrus-sasl-md5 \
      elfutils-libelf-devel \
      git \
      java-devel \
      jq \
      kernel-headers \
      libblkid-devel \
      libcurl-devel \
      libevent2-devel \
      libnl3-devel \
      libtool \
      nc \
      nss \
      openssl-devel \
      patch \
      perf \
      pkgconfig \
      python-devel \
      subversion-devel \
      tar \
      which \
      xfsprogs-devel \
      zlib-devel

# Setup JDK
RUN echo -e 'JAVA_HOME=/usr/lib/jvm/java-openjdk\n\
export JAVA_HOME' \
>> /etc/profile.d/java-home.sh

RUN yum install -y rpm-build redhat-rpm-config

RUN yum install -y ccache

RUN useradd jenkins --shell /bin/bash --create-home
USER jenkins
