#!/bin/bash
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -x
set -eu

script_dir="$(dirname "$(realpath "$0")")"
# Env vars
MESOS_DIR="${MESOS_DIR:-$(dirname $script_dir)}"

MESOS_RELEASE_NUMBER="${MESOS_RELEASE_NUMBER:-1}"
MESOS_TAG="${MESOS_TAG:-1.4.0}"

# For now, just consider MESOS_TAG.
MESOS_BRANCH=$MESOS_TAG #"${MESOS_BRANCH:-refs/heads/master}"

PUBLISH_DISTRIBUTION_PACKAGES=${PUBLISH_DISTRIBUTION_PACKAGES:-true}

PKG_OUTPUT_DIR="${PKG_OUTPUT_DIR:-pkg}"

compute_mesos_release() {
  if echo "$MESOS_BRANCH" | grep -qi master; then
    gitsha=$(cd "$MESOS_DIR" && git rev-parse --short HEAD)
    snapshot_version=$(date -u +'%Y%m%d')git$gitsha
    MESOS_RELEASE=0.$MESOS_RELEASE_NUMBER.pre.$snapshot_version
    echo "building mesos snapshot"
  elif echo "$MESOS_BRANCH" | grep -qi rc; then
    rc_version=${MESOS_BRANCH#*[-]}
    MESOS_RELEASE=0.$MESOS_RELEASE_NUMBER.$rc_version
    echo "building mesos release candidate"
  elif echo "$MESOS_BRANCH" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?$'; then
    MESOS_RELEASE=$MESOS_RELEASE_NUMBER
    echo "building mesos release"
  else
    echo "Don't know how to handle version: $MESOS_BRANCH"
    exit 1
  fi
}

run_build() {
  distro=$1
  distro_version=$2

  BUILDER_DIR=${MESOS_DIR}/packaging/${distro}/${distro_version}
  IMAGE_NAME="mesos-${distro}-${distro_version}:${MESOS_TAG}"

  mkdir -p ${BUILDER_DIR}/pkg

  echo "Using docker image $IMAGE_NAME"
  docker build -t "$IMAGE_NAME" "$BUILDER_DIR"

  MESOS_VERSION=${MESOS_TAG%[-]*}
  docker run \
    -e MESOS_TAG=$MESOS_TAG \
    -e MESOS_RELEASE=$MESOS_RELEASE \
    --net=host \
    -v "$(pwd)/common:/common:ro" \
    -v "${BUILDER_DIR}/mesos.spec:/mesos.spec:ro" \
    -v "${BUILDER_DIR}/build.sh:/build.sh:ro" \
    -v "${BUILDER_DIR}/pkg:/pkg:rw" \
    -v "/home/kapil/mesos/ccache/$distro/$distro_version:/root/.ccache:rw" \
    -t "$IMAGE_NAME" /build.sh

  ls -R ${BUILDER_DIR}/pkg
}

main() {
  compute_mesos_release

  case $# in
    2)
      distro=$1
      distro_version=$2
      ;;

    *)
      echo 'usage:'
      echo "  MESOS_TAG=<tag> MESOS_DIR=<path/to/mesos/sources> $0 <distro> <distro version>"
      exit 1
      ;;
  esac

  run_build $distro $distro_version
}

main "$@"
