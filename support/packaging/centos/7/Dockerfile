FROM centos:7
MAINTAINER Kapil Arya <kapil@apache.org>

# Get curl.
RUN yum install -y wget curl unzip

# Add the Extra packages for enterprise linux repo.
RUN yum install -y epel-release

# Add the Subversion repo.
RUN echo -e '[WANdiscoSVN]\n\
name=WANdisco SVN Repo 1.9\n\
enabled=1\n\
baseurl=http://opensource.wandisco.com/centos/7/svn-1.9/RPMS/\$basearch/\n\
gpgcheck=1\n\
gpgkey=http://opensource.wandisco.com/RPM-GPG-KEY-WANdisco' \
>> /tmp/wandisco-svn.repo
RUN mv /tmp/wandisco-svn.repo /etc/yum.repos.d/

# Add the Apache Maven repo.
RUN curl -sSL \
      http://repos.fedorapeople.org/repos/dchen/apache-maven/epel-apache-maven.repo \
      -o /etc/yum.repos.d/epel-apache-maven.repo

RUN yum makecache

# These packages conflict with Wandisco ones.
RUN yum remove -y subversion subversion-libs

# Update system.
RUN yum update -y

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
      java-1.8.0-openjdk-devel \
      jq \
      kernel-headers \
      libblkid-devel \
      libcurl-devel \
      libevent-devel \
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
      systemd-devel \
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

RUN sed -i -e's:.el7.centos:.el7:' /etc/rpm/macros.dist

RUN useradd jenkins --shell /bin/bash --create-home
#USER jenkins
