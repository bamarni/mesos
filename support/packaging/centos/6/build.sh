#!/bin/bash

MESOS_VERSION=${MESOS_TAG%[-]*}

mkdir -p $HOME/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

cp /common/* $HOME/rpmbuild/SOURCES
cp /mesos.spec $HOME/rpmbuild/SPECS

source scl_source enable devtoolset-3

ccache -M 5G

pushd $HOME/rpmbuild/SOURCES
if [ "$MESOS_VERSION" = "$MESOS_TAG" ]; then
  wget https://dist.apache.org/repos/dist/release/mesos/${MESOS_VERSION}/mesos-${MESOS_VERSION}.tar.gz
else
  https://dist.apache.org/repos/dist/dev/mesos/${MESOS_TAG}/mesos-${MESOS_VERSION}.tar.gz
fi
popd

export PATH=/usr/lib64/ccache:$PATH
rpmbuild --verbose --define "MESOS_VERSION $MESOS_VERSION" --define "MESOS_RELEASE $MESOS_RELEASE" -ba $HOME/rpmbuild/SPECS/mesos.spec

cp $HOME/rpmbuild/SRPMS/* /pkg
cp $HOME/rpmbuild/RPMS/*/* /pkg
