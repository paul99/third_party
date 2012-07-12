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

// Defines callback types for the invalidation client library.

#ifndef GOOGLE_CACHEINVALIDATION_DEPS_CALLBACK_H_
#define GOOGLE_CACHEINVALIDATION_DEPS_CALLBACK_H_

#include "base/callback.h"

#define INVALIDATION_CALLBACK1_TYPE(Arg1) ::Callback1<Arg1>

namespace invalidation {

using ::Closure;
using ::DoNothing;
using ::NewPermanentCallback;

template <class T>
bool IsCallbackRepeatable(const T* callback) {
  return callback->IsRepeatable();
}

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_DEPS_CALLBACK_H_
