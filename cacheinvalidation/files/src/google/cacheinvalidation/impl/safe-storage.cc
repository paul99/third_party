// Copyright 2012 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// An implementation of the Storage resource that schedules the callbacks on the
// given scheduler thread.
//

#include "google/cacheinvalidation/impl/safe-storage.h"

namespace {

// A procedure to run a callback and then delete it.
template<typename CallbackType, typename ArgumentType>
void RunAndDeleteStorageCallback(CallbackType* callback, ArgumentType arg) {
  callback->Run(arg);
  delete callback;
}

}  // namespace

namespace invalidation {

void SafeStorage::SetSystemResources(SystemResources* resources) {
  scheduler_ = resources->internal_scheduler();
}

void SafeStorage::WriteKey(const string& key, const string& value,
    WriteKeyCallback* done) {
  delegate_->WriteKey(key, value,
      NewPermanentCallback(this, &SafeStorage::WriteCallback, done));
}

void SafeStorage::WriteCallback(WriteKeyCallback* done, Status status) {
  scheduler_->Schedule(Scheduler::NoDelay(), NewPermanentCallback(
      &RunAndDeleteStorageCallback<WriteKeyCallback, Status>, done, status));
}

void SafeStorage::ReadKey(const string& key, ReadKeyCallback* done) {
  delegate_->ReadKey(key,
      NewPermanentCallback(this, &SafeStorage::ReadCallback, done));
}

void SafeStorage::ReadCallback(ReadKeyCallback* done,
    StatusStringPair read_result) {
  scheduler_->Schedule(Scheduler::NoDelay(), NewPermanentCallback(
      &RunAndDeleteStorageCallback<ReadKeyCallback, StatusStringPair>, done,
      read_result));
}

void SafeStorage::DeleteKey(const string& key, DeleteKeyCallback* done) {
  delegate_->DeleteKey(key,
      NewPermanentCallback(this, &SafeStorage::DeleteCallback, done));
}

void SafeStorage::DeleteCallback(DeleteKeyCallback* done, bool result) {
  scheduler_->Schedule(Scheduler::NoDelay(), NewPermanentCallback(
      &RunAndDeleteStorageCallback<DeleteKeyCallback, bool>, done, result));
}

void SafeStorage::ReadAllKeys(ReadAllKeysCallback* key_callback) {
  delegate_->ReadAllKeys(
      NewPermanentCallback(this, &SafeStorage::ReadAllCallback, key_callback));
}

void SafeStorage::ReadAllCallback(ReadAllKeysCallback* key_callback,
    StatusStringPair result) {
  scheduler_->Schedule(Scheduler::NoDelay(), NewPermanentCallback(
      &RunAndDeleteStorageCallback<ReadAllKeysCallback, StatusStringPair>,
      key_callback, result));
}

}  // namespace invalidation
