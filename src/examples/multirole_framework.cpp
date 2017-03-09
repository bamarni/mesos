// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <mesos/resources.hpp>
#include <mesos/scheduler.hpp>
#include <mesos/type_utils.hpp>

#include <process/dispatch.hpp>
#include <process/process.hpp>

#include <stout/check.hpp>
#include <stout/duration.hpp>
#include <stout/exit.hpp>
#include <stout/flags.hpp>
#include <stout/foreach.hpp>
#include <stout/json.hpp>
#include <stout/option.hpp>
#include <stout/os.hpp>
#include <stout/protobuf.hpp>
#include <stout/stringify.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>

#include <glog/logging.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <fstream>

#include "common/parse.hpp"
#include "common/protobuf_utils.hpp"
#include "logging/logging.hpp"
#include "v1/parse.hpp"

namespace {
std::string oneline(const std::string& s)
{
  constexpr char DEL[] = ", ";
  return strings::trim(strings::replace(s, "\n", DEL), strings::SUFFIX, DEL);
}

constexpr char STATE_FILE[] = "framework_id";

Try<mesos::FrameworkID> recoverFrameworkId()
{
  std::ifstream file(STATE_FILE);

  std::string value;
  if (!std::getline(file, value)) {
    return Error("Could not recover FrameworkID");
  }

  return flags::parse<mesos::FrameworkID>(value);
}

void stashFrameworkId(const mesos::FrameworkID& frameworkId)
{
  std::ofstream file(STATE_FILE);
  CHECK(file.is_open());
  file << stringify(frameworkId);
}
} // namespace {

namespace mesos {
std::ostream& operator<<(std::ostream& stream, const Offer& offer)
{
  return stream << oneline(offer.DebugString());
}


std::ostream& operator<<(std::ostream& stream, const TaskStatus& status)
{
  return stream << oneline(status.DebugString());
}


std::ostream& operator<<(std::ostream& stream, const Credential& credential)
{
  return stream << oneline(credential.DebugString());
}
} // namespace mesos {

struct TaskWithRole
{
  mesos::TaskInfo taskInfo;
  std::string role;
  bool await = true;
};


bool operator==(const TaskWithRole& lhs, const TaskWithRole& rhs)
{
  return std::tie(lhs.taskInfo, lhs.role, lhs.await) ==
         std::tie(rhs.taskInfo, rhs.role, rhs.await);
}


std::deque<TaskWithRole> extractTaskWithRole(const JSON::Array& array)
{
  std::deque<TaskWithRole> tasks;

  foreach (const JSON::Value& value, array.values) {
    TaskWithRole taskWithRole;

    CHECK(value.is<JSON::Object>()) << "Task is not a JSON object";
    auto value_ = value.as<JSON::Object>();

    Result<JSON::String> role = value_.at<JSON::String>("role");
    CHECK_SOME(role) << "Could not find 'role'";
    taskWithRole.role = role->value;

    Result<JSON::Object> task = value_.at<JSON::Object>("task");
    CHECK_SOME(task) << "Could not find 'task'";
    auto task_ = protobuf::parse<mesos::TaskInfo>(task.get());
    if (task_.isError()) {
      EXIT(EXIT_FAILURE) << "Invalid task definition: " << task.error();
    }
    taskWithRole.taskInfo = task_.get();

    Result<JSON::Boolean> await = value_.at<JSON::Boolean>("await");
    if (await.isSome()) {
      taskWithRole.await = await->value;
    }

    tasks.push_back(taskWithRole);
  }

  return tasks;
}


struct Flags : public virtual flags::FlagsBase
{
  Flags()
  {
    add(&Flags::master, "master", "Master to connect to");
    add(&Flags::roles,
        "roles",
        "Array of roles the framework should register with");
    add(&Flags::tasks_,
        "tasks",
        "Tasks to run. Each task needs to have an associated role. A task's\n"
        "`slave_id` field can be left empty.\n"
        "\n"
        "Example:\n"
        "{\n"
        "  \"tasks\": [\n"
        "    {\n"
        "      \"role\": \"roleA\",\n"
        "      \"task\": {\n"
        "        \"command\": { \"value\": \"sleep 3\" },\n"
        "        \"name\": \"task1\",\n"
        "        \"task_id\": { \"value\": \"task1\" },\n"
        "        \"resources\": [\n"
        "          {\n"
        "            \"name\": \"cpus\",\n"
        "            \"role\": \"*\",\n"
        "            \"scalar\": {\n"
        "              \"value\": 0.1\n"
        "            },\n"
        "            \"type\": \"SCALAR\"\n"
        "          },\n"
        "          {\n"
        "            \"name\": \"mem\",\n"
        "            \"role\": \"*\",\n"
        "            \"scalar\": {\n"
        "              \"value\": 32\n"
        "            },\n"
        "            \"type\": \"SCALAR\"\n"
        "          }\n"
        "        ],\n"
        "        \"slave_id\": { \"value\": \"\" }\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}");
    add(&Flags::maxUnsuccessfulOfferCycles,
        "max_unsuccessful_offer_cycles",
        "The maximal number of offer cycles without allocatable offers\n"
        "before the scheduler will exit.");
  }

  std::string master;

  Option<JSON::Array> roles;

  JSON::Object tasks_;
  std::deque<TaskWithRole> tasks;

  Option<size_t> maxUnsuccessfulOfferCycles;
};


class MultiRoleSchedulerProcess
    : public process::Process<MultiRoleSchedulerProcess>
{
public:
  MultiRoleSchedulerProcess(
      const Flags& _flags, const mesos::FrameworkInfo& _frameworkInfo)
    : flags(_flags), frameworkInfo(_frameworkInfo), waitingTasks(_flags.tasks)
  {}

  void registered(
      mesos::SchedulerDriver* driver,
      const mesos::FrameworkID& frameworkId,
      const mesos::MasterInfo& masterInfo)
  {
    stashFrameworkId(frameworkId);

    driver->reconcileTasks({});
  }

  void resourceOffers(
      mesos::SchedulerDriver* driver, const std::vector<mesos::Offer>& offers)
  {
    bool usedOffers = false;

    foreach (const mesos::Offer& offer, offers) {
      if (waitingTasks.empty()) {
        driver->declineOffer(offer.id());

        usedOffers = true;

        continue;
      }

      // Determine the role the offer was made for.
      Option<std::string> resourcesRole_ = None();
      foreach (const mesos::Resource& resource, offer.resources()) {
        CHECK(resource.has_allocation_info());
        CHECK(resource.allocation_info().has_role());

        if (resourcesRole_.isNone()) {
          resourcesRole_ = resource.allocation_info().role();
        } else {
          CHECK_EQ(resourcesRole_.get(), resource.allocation_info().role());
        }
      }
      CHECK_SOME(resourcesRole_);

      const std::string resourcesRole = resourcesRole_.get();
      const mesos::Resources resources(offer.resources());

      LOG(INFO) << "With " << waitingTasks.size()
                << " unscheduled tasks, looking for tasks to run on resources "
                   "on agent '"
                << offer.slave_id() << "': " << stringify(resources);

      // Find waiting tasks matching the role this allocation was made to.
      std::vector<TaskWithRole> candidateTasks;
      std::copy_if(
          waitingTasks.begin(),
          waitingTasks.end(),
          std::back_inserter(candidateTasks),
          [&resourcesRole](const TaskWithRole& task) {
            return task.role == resourcesRole;
          });

      if (candidateTasks.empty()) {
        // Decline offer if there is no work to do.
        LOG(INFO) << "No tasks can run on '" << stringify(resources.flatten())
                  << "'";
        driver->declineOffer(offer.id());
      } else {
        // Launch the task and transition it from waiting to running.
        TaskWithRole candidateTask = candidateTasks[0];
        const std::string role = candidateTask.role;

        mesos::TaskInfo task = candidateTask.taskInfo;
        task.mutable_slave_id()->CopyFrom(offer.slave_id());

        {
          // Calculate resources to use.
          // TODO(bbannier): pick a task that actually fits on the offer
          // instead of just sending a potentially too big or too small task on
          // a big offer.
          mesos::Resources taskResources = task.resources();
          taskResources.allocate(role);
          mesos::Resources remaining = offer.resources();
          Try<mesos::Resources> flattened =
            taskResources.flatten(role);
          CHECK_SOME(flattened);

          Option<mesos::Resources> resources = remaining.find(flattened.get());
          CHECK_SOME(resources);

          task.mutable_resources()->CopyFrom(resources.get());
        }

        driver->launchTasks(offer.id(), {task});
        LOG(INFO) << "Launched task '" << task.task_id() << "' with role '"
                  << role << "' to run on resources allocated for role '"
                  << resourcesRole << "'";

        CHECK(
            std::find(
                runningTasks.begin(),
                runningTasks.end(),
                candidateTask.taskInfo.task_id()) == runningTasks.end());

        // Remember this task if we want to wait for it. Otherwise
        // leak it here, and e.g., recover it in reconciliation.
        if (candidateTask.await) {
          runningTasks.push_back(candidateTask.taskInfo.task_id());
        }

        waitingTasks.erase(
            std::remove_if(
                waitingTasks.begin(),
                waitingTasks.end(),
                [&candidateTask](const TaskWithRole& task) {
                  return task == candidateTask;
                }));

        usedOffers = true;
        numUnsuccessfulOfferCycles = 0;
      }
    }

    if (waitingTasks.empty() && runningTasks.empty()) {
      driver->stop(true);
      return;
    }

    if (!usedOffers) {
      ++numUnsuccessfulOfferCycles;
      if (flags.maxUnsuccessfulOfferCycles.isSome() &&
          numUnsuccessfulOfferCycles >=
            flags.maxUnsuccessfulOfferCycles.get()) {
        LOG(ERROR)
          << "Unsuccessfully tried for " << numUnsuccessfulOfferCycles
          << " offer cycles to run remaining tasks, aborting scheduler.";
        driver->abort();
      }
    }
  }

  void statusUpdate(
      mesos::SchedulerDriver* driver, const mesos::TaskStatus& status)
  {
    auto it =
      std::find(runningTasks.begin(), runningTasks.end(), status.task_id());

    // If we receive a status update for an unknown task, assume we
    // are dealing with a task we plan to leak and recover when
    // restarting the framework later on.
    if (it == runningTasks.end()) {
      if (status.message() == "Reconciliation: Latest task state") {
        LOG(INFO) << "Learned about running task"
                     " '" + stringify(status.task_id()) + "'";
        runningTasks.push_back(status.task_id());
        driver->killTask(status.task_id());
      }

      return;
    } else {
      if (mesos::internal::protobuf::isTerminalState(status.state())) {
        LOG(INFO) << "Task '" << status.task_id()
                  << "' has reached terminal state '"
                  << stringify(status.state()) << "'";
        runningTasks.erase(it);
      } else {
        LOG(INFO) << "Task '" << status.task_id()
                  << "' has transitioned to state '"
                  << stringify(status.state()) << "'";
      }

      if (status.state() == mesos::TASK_ERROR) {
        LOG(ERROR) << "Task '" << status.task_id()
                   << "' had an error: " << status.message();
        driver->abort();
      }
    }
  }

private:
  const Flags flags;
  const mesos::FrameworkInfo frameworkInfo;

  std::deque<TaskWithRole> waitingTasks;
  std::deque<mesos::TaskID> runningTasks;

  size_t numUnsuccessfulOfferCycles = 0;
};


class MultiRoleScheduler : public mesos::Scheduler
{
public:
  MultiRoleScheduler(
      const Flags& flags, const mesos::FrameworkInfo& frameworkInfo)
    : process(flags, frameworkInfo)
  {
    process::spawn(process);
  }

  virtual ~MultiRoleScheduler()
  {
    process::terminate(process);
    process::wait(process);
  }

private:
  MultiRoleSchedulerProcess process;

  void registered(
      mesos::SchedulerDriver* driver,
      const mesos::FrameworkID& frameworkId,
      const mesos::MasterInfo& masterInfo) override
  {
    LOG(INFO) << "Registered with framework ID: " << frameworkId;
    process::dispatch(
        process,
        &MultiRoleSchedulerProcess::registered,
        driver,
        frameworkId,
        masterInfo);
  }

  void reregistered(
      mesos::SchedulerDriver* driver,
      const mesos::MasterInfo& masterInfo) override
  {
    driver->abort();
    EXIT(EXIT_FAILURE) << "MultiRoleScheduler::reregistered: "
                       << stringify(masterInfo);
  }

  void disconnected(mesos::SchedulerDriver* driver) override
  {
    driver->abort();
    EXIT(EXIT_FAILURE) << "MultiRoleScheduler::disconnected";
  }

  void resourceOffers(
      mesos::SchedulerDriver* driver,
      const std::vector<mesos::Offer>& offers) override
  {
    LOG(INFO) << "Resource offers received";

    process::dispatch(
        process, &MultiRoleSchedulerProcess::resourceOffers, driver, offers);
  }

  void offerRescinded(
      mesos::SchedulerDriver* driver, const mesos::OfferID& offerId) override
  {
    driver->abort();
    EXIT(EXIT_FAILURE) << "MultiRoleScheduler::offerRescinded: "
                       << stringify(offerId);
  }

  void statusUpdate(
      mesos::SchedulerDriver* driver, const mesos::TaskStatus& status) override
  {
    LOG(INFO) << "Received status update for task '" << status.task_id() << "'";
    process::dispatch(
        process, &MultiRoleSchedulerProcess::statusUpdate, driver, status);
  }

  void frameworkMessage(
      mesos::SchedulerDriver* driver,
      const mesos::ExecutorID& executorId,
      const mesos::SlaveID& slaveId,
      const std::string& data) override
  {
    driver->abort();
    EXIT(EXIT_FAILURE) << "MultiRoleScheduler::frameworkMessage: "
                       << stringify(executorId) << " " << stringify(slaveId)
                       << " " << stringify(data);
  }

  void slaveLost(
      mesos::SchedulerDriver* driver, const mesos::SlaveID& slaveId) override
  {
    driver->abort();
    EXIT(EXIT_FAILURE) << "MultiRoleScheduler::slaveLost: "
                       << stringify(slaveId);
  }

  void executorLost(
      mesos::SchedulerDriver* driver,
      const mesos::ExecutorID& executorId,
      const mesos::SlaveID& slaveId,
      int status) override
  {
    driver->abort();
    EXIT(EXIT_FAILURE) << "MultiRoleScheduler::executorLost: "
                       << stringify(executorId) << " " << stringify(slaveId);
  }

  void error(
      mesos::SchedulerDriver* driver, const std::string& message) override
  {
    driver->abort();
    EXIT(EXIT_FAILURE) << "MultiRoleScheduler::error: " << message;
  }
};


int main(int argc, char** argv)
{
  process::initialize();

  Flags flags;
  Try<flags::Warnings> load = flags.load("MESOS_", argc, argv);

  if (load.isError()) {
    EXIT(EXIT_FAILURE) << flags.usage(load.error());
  }

  if (flags.help) {
    EXIT(EXIT_SUCCESS) << flags.usage();
  }

  auto tasks = flags.tasks_.at<JSON::Array>("tasks");
  CHECK_SOME(tasks) << "Could not extract tasks";

  flags.tasks = extractTaskWithRole(tasks.get());

  // Configure Mesos-facing side of the framework.
  mesos::FrameworkInfo framework;
  framework.set_user(""); // Have Mesos fill the current user.
  framework.set_name("Multi-role framework (C++)");
  framework.set_checkpoint(true);

  framework.add_capabilities()->set_type(
      mesos::FrameworkInfo::Capability::MULTI_ROLE);

  if (flags.roles.isSome()) {
    LOG(INFO) << "Running framework with roles "
              << stringify(flags.roles->values);
    foreach (const JSON::Value& value, flags.roles->values) {
      CHECK(value.is<JSON::String>()) << "Could not parse roles value '"
                                      << stringify(value) << "' as string";
      framework.add_roles(value.as<JSON::String>().value);
    }
  }

  // If we find a persisted framework id reuse it. This allows tests
  // of reregistration behavior.
  Try<mesos::FrameworkID> frameworkId = recoverFrameworkId();
  if (frameworkId.isSome()) {
    framework.mutable_id()->CopyFrom(frameworkId.get());
  }

  // Prevent the framework from being immediately garbage collected on
  // disconnect in order to allow framework failovers (e.g., to test
  // reregistation behavior).
  framework.set_failover_timeout(Days(10).secs());

  LOG(INFO) << "Scheduling tasks: " << flags.tasks_;

  MultiRoleScheduler scheduler(flags, framework);

  std::unique_ptr<mesos::MesosSchedulerDriver> driver;

  // Set up authentication if used.
  // TODO(hartem): Refactor these into a common set of flags.
  Option<std::string> value = os::getenv("MESOS_AUTHENTICATE_FRAMEWORKS");
  if (value.isSome()) {
    LOG(INFO) << "Enabling authentication for the framework";

    value = os::getenv("DEFAULT_PRINCIPAL");
    if (value.isNone()) {
      EXIT(EXIT_FAILURE)
          << "Expecting authentication principal in the environment";
    }

    mesos::Credential credential;
    credential.set_principal(value.get());

    framework.set_principal(value.get());

    value = os::getenv("DEFAULT_SECRET");
    if (value.isNone()) {
      EXIT(EXIT_FAILURE)
          << "Expecting authentication secret in the environment";
    }

    credential.set_secret(value.get());

    LOG(INFO) << "Using credential '" << stringify(credential) << "'";

    driver.reset(
        new mesos::MesosSchedulerDriver(
            &scheduler, framework, flags.master, credential));
  } else {
    framework.set_principal("multirole-framework-cpp");

    driver.reset(
        new mesos::MesosSchedulerDriver(&scheduler, framework, flags.master));
  }

  // Run the framework and report its status to the caller.
  int status = driver->run() != mesos::DRIVER_STOPPED;

  return status;
}
