// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/dispatch_story_command_executor.h"

#include <lib/syslog/cpp/macros.h>

#include <map>

#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/async/cpp/operation.h"

namespace modular {

namespace {

class RunStoryCommandCall : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  RunStoryCommandCall(const char* const command_name, CommandRunner* const runner,
                      StoryStorage* const story_storage, fidl::StringPtr story_id,
                      fuchsia::modular::StoryCommand command, ResultCall done)
      : Operation(command_name, std::move(done), ""),
        command_(std::move(command)),
        story_storage_(story_storage),
        story_id_(std::move(story_id)),
        runner_(runner) {}

 private:
  // |OperationBase|
  void Run() override {
    auto done = [this](fuchsia::modular::ExecuteResult result) { Done(std::move(result)); };
    runner_->Execute(story_id_, story_storage_, std::move(command_), std::move(done));
  }

  fuchsia::modular::StoryCommand command_;
  StoryStorage* const story_storage_;
  const fidl::StringPtr story_id_;
  CommandRunner* runner_;
};

}  // namespace

class DispatchStoryCommandExecutor::ExecuteStoryCommandsCall
    : public Operation<fuchsia::modular::ExecuteResult> {
 public:
  ExecuteStoryCommandsCall(DispatchStoryCommandExecutor* const executor, fidl::StringPtr story_id,
                           std::vector<fuchsia::modular::StoryCommand> commands, ResultCall done)
      : Operation("ExecuteStoryCommandsCall", std::move(done)),
        executor_(executor),
        story_id_(std::move(story_id)),
        commands_(std::move(commands)) {}

  ~ExecuteStoryCommandsCall() override = default;

 private:
  void Run() override {
    auto story_storage = executor_->session_storage_->GetStoryStorage(story_id_);
    if (!story_storage) {
      fuchsia::modular::ExecuteResult result;
      result.status = fuchsia::modular::ExecuteStatus::INVALID_STORY_ID;
      Done(result);
      return;
    }

    story_storage_ = std::move(story_storage);
    Cont();
  }

  void Cont() {
    // TODO(thatguy): Add a WeakPtr check on |executor_|.

    // Keep track of the number of commands we need to run. When they are all
    // done, we complete this operation.
    std::vector<FuturePtr<>> did_execute_commands;
    did_execute_commands.reserve(commands_.size());

    for (auto& command : commands_) {
      auto tag_string_it = executor_->story_command_tag_strings_.find(command.Which());
      FX_CHECK(tag_string_it != executor_->story_command_tag_strings_.end())
          << "No fuchsia::modular::StoryCommand::Tag string for tag "
          << static_cast<int>(command.Which());
      const auto& tag_string = tag_string_it->second;

      auto it = executor_->command_runners_.find(command.Which());
      FX_DCHECK(it != executor_->command_runners_.end())
          << "Could not find a fuchsia::modular::StoryCommand runner for tag "
          << static_cast<int>(command.Which()) << ": " << tag_string;

      auto* const command_runner = it->second.get();
      // NOTE: it is safe to capture |this| on the lambdas below because if
      // |this| goes out of scope, |queue_| will be deleted, and the callbacks
      // on |queue_| will not run.

      auto did_execute_command = Future<fuchsia::modular::ExecuteResult>::Create(
          "DispatchStoryCommandExecutor.ExecuteStoryCommandsCall.Run.did_"
          "execute_command");
      queue_.Add(std::make_unique<RunStoryCommandCall>(
          tag_string, command_runner, story_storage_.get(), story_id_, std::move(command),
          did_execute_command->Completer()));
      auto did_execute_command_callback =
          did_execute_command->Then([this](fuchsia::modular::ExecuteResult result) {
            // Check for error for this command. If there was an error, abort
            // early. All of the remaining operations (if any) in queue_ will
            // not be run.
            if (result.status != fuchsia::modular::ExecuteStatus::OK) {
              Done(std::move(result));
            }
          });
      did_execute_commands.emplace_back(did_execute_command_callback);
    }

    Wait("DispatchStoryCommandExecutor.ExecuteStoryCommandsCall.Run.Wait", did_execute_commands)
        ->Then([this] {
          fuchsia::modular::ExecuteResult result;
          result.status = fuchsia::modular::ExecuteStatus::OK;
          result.story_id = story_id_;
          Done(std::move(result));
        });
  }

  DispatchStoryCommandExecutor* const executor_;
  const fidl::StringPtr story_id_;
  std::vector<fuchsia::modular::StoryCommand> commands_;

  std::shared_ptr<StoryStorage> story_storage_;

  // All commands must be run in order so we use a queue.
  OperationQueue queue_;
};

DispatchStoryCommandExecutor::DispatchStoryCommandExecutor(
    SessionStorage* const session_storage,
    std::map<fuchsia::modular::StoryCommand::Tag, std::unique_ptr<CommandRunner>> command_runners)
    : session_storage_(session_storage),
      command_runners_(std::move(command_runners)),
      story_command_tag_strings_{
          {fuchsia::modular::StoryCommand::Tag::kAddMod, "StoryCommand::AddMod"},
          {fuchsia::modular::StoryCommand::Tag::kFocusMod, "StoryCommand::FocusMod"},
          {fuchsia::modular::StoryCommand::Tag::kRemoveMod, "StoryCommand::RemoveMod"},
          {fuchsia::modular::StoryCommand::Tag::kSetLinkValue, "StoryCommand::SetLinkValue"},
          {fuchsia::modular::StoryCommand::Tag::kSetFocusState, "StoryCommand::SetFocusState"}} {
  FX_DCHECK(session_storage_ != nullptr);
}

DispatchStoryCommandExecutor::~DispatchStoryCommandExecutor() {}

void DispatchStoryCommandExecutor::ExecuteCommandsInternal(
    fidl::StringPtr story_id, std::vector<fuchsia::modular::StoryCommand> commands,
    fit::function<void(fuchsia::modular::ExecuteResult)> done) {
  operation_queues_[story_id].Add(std::make_unique<ExecuteStoryCommandsCall>(
      this, std::move(story_id), std::move(commands), std::move(done)));
}

}  // namespace modular
