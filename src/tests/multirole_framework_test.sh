#!/usr/bin/env bash

# FIXME(bbannier): module docstring

set -e
set -o pipefail

function random_port {
  # Generate a random port number.
  echo $((RANDOM + 2000))
}

function setup_env {
  # shellcheck source=/dev/null
  source "${MESOS_SOURCE_DIR}"/support/colors.sh
  # shellcheck source=/dev/null
  source "${MESOS_SOURCE_DIR}"/support/atexit.sh

  export LD_LIBRARY_PATH=${MESOS_BUILD_DIR}/src/.libs
  MASTER=${MESOS_SBIN_DIR}/mesos-master
  AGENT=${MESOS_SBIN_DIR}/mesos-agent
  MULTIROLE_FRAMEWORK=${MESOS_HELPER_DIR}/multirole-framework

  # The mesos binaries expect MESOS_ prefixed environment variables
  # to correspond to flags, so we unset these here.
  unset MESOS_BUILD_DIR
  unset MESOS_SOURCE_DIR
  unset MESOS_HELPER_DIR
  unset MESOS_VERBOSE

  # Disable authenticating so we can create principals on the fly in below test.
  unset MESOS_AUTHENTICATE
  unset MESOS_AUTHENTICATE_FRAMEWORKS
}

function start_master {
  MESOS_WORK_DIR=$(mktemp -d -t mesos-master-XXXXXX)
  atexit rm -rf "${MESOS_WORK_DIR}"

  MASTER_PORT=$(random_port)

  ACLS=${1:-\{\"permissive\": true\}}

  ${MASTER} \
    --ip=127.0.0.1 \
    --port="$MASTER_PORT" \
    --acls="${ACLS}" \
    --work_dir="${MESOS_WORK_DIR}" &> "${MESOS_WORK_DIR}.log" &
  MASTER_PID=${!}

  atexit rm -rf "${MESOS_WORK_DIR}.log"

  echo "${GREEN}Launched master at ${MASTER_PID}${NORMAL}"

  sleep 2

  # Check the master is still running after 2 seconds.
  kill -0 ${MASTER_PID} >/dev/null 2>&1
  STATUS=${?}
  if [[ ${STATUS} -ne 0 ]]; then
    echo "${RED}Master crashed; failing test${NORMAL}"
    exit 2
  fi

  atexit kill ${MASTER_PID}
}

function start_agent {
  # Disable support for systemd as this test does not run as root.
  # This flag must be set as an environment variable because the flag
  # does not exist on non-Linux builds.
  export MESOS_SYSTEMD_ENABLE_SUPPORT=false

  MESOS_WORK_DIR=$(mktemp -d -t mesos-agent-XXXXXX)
  atexit rm -rf "${MESOS_WORK_DIR}"

  MESOS_RUNTIME_DIR=$(mktemp -d -t mesos-agent-runtime-XXXXXX)
  atexit rm -rf "${MESOS_RUNTIME_DIR}"

  AGENT_PORT=$(random_port)

  RESOURCES=${1:-cpus:1;mem:96;disk:50}

  ${AGENT} \
    --work_dir="${MESOS_WORK_DIR}" \
    --runtime_dir="${MESOS_RUNTIME_DIR}" \
    --master=127.0.0.1:"$MASTER_PORT" \
    --port="$AGENT_PORT" \
    --resources="${RESOURCES}" &> "${MESOS_WORK_DIR}.log" &
  AGENT_PID=${!}

  atexit rm -rf "${MESOS_WORK_DIR}.log"

  echo "${GREEN}Launched agent at ${AGENT_PID}${NORMAL}"

  sleep 2

  # Check the agent is still running after 2 seconds.
  kill -0 ${AGENT_PID} >/dev/null 2>&1
  STATUS=${?}
  if [[ ${STATUS} -ne 0 ]]; then
    echo "${RED}Slave crashed; failing test${NORMAL}"
    exit 2
  fi

  atexit kill ${AGENT_PID}
}

function run_framework {
  ROLES=${1:-\[\"roleA\", \"roleB\"\]}
  DEFAULT_TASKS='
      {
        "tasks": [
          {
            "role": "roleA",
            "task": {
              "command": { "value": "sleep 1" },
              "name": "task1",
              "task_id": { "value": "task1" },
              "resources": [
                {
                  "name": "cpus",
                  "scalar": {
                    "value": 0.5
                  },
                  "type": "SCALAR"
                },
                {
                  "name": "mem",
                  "scalar": {
                    "value": 48
                  },
                  "type": "SCALAR"
                }
              ],
              "slave_id": { "value": "" }
            }
          },
          {
            "role": "roleB",
            "task": {
              "command": { "value": "sleep 1" },
              "name": "task2",
              "task_id": { "value": "task2" },
              "resources": [
                {
                  "name": "cpus",
                  "scalar": {
                    "value": 0.5
                  },
                  "type": "SCALAR"
                },
                {
                  "name": "mem",
                  "scalar": {
                    "value": 48
                  },
                  "type": "SCALAR"
                }
              ],
              "slave_id": { "value": "" }
            }
          }
        ]
      }'

  MESOS_TASKS=${MESOS_TASKS:-$DEFAULT_TASKS}

  ${MULTIROLE_FRAMEWORK} \
    --master=127.0.0.1:"$MASTER_PORT" \
    --roles="$ROLES" \
    --max_unsuccessful_offer_cycles=3 \
    --tasks="${MESOS_TASKS}"
}

setup_env

function test_1 {
  echo "${BOLD}"
  echo "********************************************************************************************"
  echo "* A framework can be in two roles and start tasks on resources allocated for either role.  *"
  echo "********************************************************************************************"
  echo "${NORMAL}"
  start_master
  start_agent
  run_framework
}

function test_quota {
  echo "${BOLD}"
  echo "********************************************************************************************"
  echo "* Frameworks in multiple roles can use quota.                                              *"
  echo "********************************************************************************************"
  echo "${NORMAL}"
  start_master
  start_agent

  echo "${BOLD}"
  echo "Quota'ing all of the agent's resources for 'roleA'."
  echo "${NORMAL}"
  QUOTA='
  {
    "role": "roleA",
    "force": true,
    "guarantee": [
    {
      "name": "cpus",
      "type": "SCALAR",
      "scalar": { "value": 1}
    },
    {
      "name": "mem",
      "type": "SCALAR",
      "scalar": { "value": 96}
    },
    {
      "name": "disk",
      "type": "SCALAR",
      "scalar": { "value": 50}
    }
    ]
  }'

  curl --verbose -d"${QUOTA}" http://127.0.0.1:"$MASTER_PORT"/quota

  echo "${BOLD}"
  echo The framework will not get any resources to run tasks with 'roleB'.
  echo "${NORMAL}"
  [ ! "$(run_framework)" ]

  echo "${BOLD}"
  echo If we make more resources available, the framework will also be offered resources for 'roleB'.
  echo "${NORMAL}"
  start_agent

  run_framework
}

function test_reserved_resources {
  echo "${BOLD}"
  echo "********************************************************************************************"
  echo "* Reserved resources.                                                                      *"
  echo "********************************************************************************************"
  echo "${NORMAL}"
  start_master

  echo "${BOLD}"
  RESOURCES="cpus(roleA):0.5;cpus(roleB):0.5;mem(roleA):48;mem(roleB):48;disk(roleA):25;disk(roleB):25"
  echo Starting agent with reserved resources: $RESOURCES.
  echo We expect a framework in both roles to be able to launch tasks on both resources from either role.
  echo "${NORMAL}"
  start_agent "${RESOURCES}"
  run_framework
}

function test_fair_share {
  echo "${BOLD}"
  echo "********************************************************************************************"
  echo "* Fair share.                                                                              *"
  echo "********************************************************************************************"
  echo "${NORMAL}"
  start_master
  start_agent "cpus:0.5;mem:48;disk:25"
  start_agent "cpus:0.5;mem:48;disk:25"
  start_agent "cpus:0.5;mem:48;disk:25"

  echo "${BOLD}"
  echo Starting a framework in two roles which will consume the bulk on the resources.
  echo "${NORMAL}"
  run_framework &

  echo "${BOLD}"
  echo Starting a framework in just one role which will be offered not enough
  echo resources since the earlier will be below fair share in that role.
  echo "${NORMAL}"
  [ ! "$(run_framework '["roleA"]')" ]
}

function test_framework_authz {
  echo "${BOLD}"
  echo "********************************************************************************************"
  echo "* Framework authorization.                                                                 *"
  echo "********************************************************************************************"
  echo "${NORMAL}"

  ACLS='
  {
    "register_frameworks": [
      {
        "principals": { "values": ["'${DEFAULT_PRINCIPAL}'"] },
        "roles": { "values": ["roleA"] }
      },
      {
        "principals": { "values": ["OTHER_PRINCIPAL"] },
        "roles": { "values" : ["roleB"] }
      }
    ]
  }
  '
  start_master "${ACLS}"
  start_agent

  echo "${BOLD}"
  echo "Attempting to register a framework in role 'roleA' with a"
  echo "principal authorized authorized for the role succeeds. The framework"
  echo "can run tasks."
  echo "${NORMAL}"
  (DEFAULT_PRINCIPAL=OTHER_PRINCIPAL MESOS_TASKS='{"tasks": []}' && run_framework '["roleB"]')

  echo "${BOLD}"
  echo "Attempting to register a framework in roles ['roleA', 'roleB'] with a principal authorized only for 'roleB' fails."
  echo "${NORMAL}"
  [ ! "$(DEFAULT_PRINCIPAL=OTHER_PRINCIPAL && run_framework)" ]

  echo "${BOLD}"
  echo "Attempting to register a framework in roles ['roleA', 'roleB'] with a"
  echo "principal authorized authorized for both roles succeeds. The framework can"
  echo "run tasks."
  echo "${NORMAL}"
  run_framework
}

# test_1

test_reserved_resources
test_fair_share
test_quota
test_framework_authz
