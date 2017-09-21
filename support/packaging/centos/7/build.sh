#!/bin/bash
set -x

MESOS_VERSION=${MESOS_TAG%[-]*}

RPMBUILD_DIR=$PWD/rpmbuild
mkdir -p $RPMBUILD_DIR/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

cp support/packaging/common/* $RPMBUILD_DIR/SOURCES
cp support/packaging/mesos.spec $RPMBUILD_DIR/SPECS

pushd $HOME/rpmbuild/SOURCES
if [ "$MESOS_VERSION" = "$MESOS_TAG" ]; then
  wget -nv https://dist.apache.org/repos/dist/release/mesos/${MESOS_VERSION}/mesos-${MESOS_VERSION}.tar.gz
else
  wget -nv https://dist.apache.org/repos/dist/dev/mesos/${MESOS_TAG}/mesos-${MESOS_VERSION}.tar.gz
fi
popd

rpmbuild --buildroot $RPMBUILD_DIR --define "MESOS_VERSION $MESOS_VERSION" --define "MESOS_RELEASE $MESOS_RELEASE" -ba $RPMBUILD_DIR/SPECS/mesos.spec
