/*
 * Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.ipc.invalidation.ticl;

import com.google.common.base.Preconditions;

/**
 * An abstraction that keeps track of whether the caller is started or stopped and only allows
 * the following transitions NOT_STARTED -> STARTED -> STOPPED. This class is thread-safe.
 *
 *
 */
public class RunState {

   /** Whether the instance has been started and/or stopped. */
  private enum CurrentState {
    NOT_STARTED,
    STARTED,
    STOPPED
  }

  private CurrentState currentState = CurrentState.NOT_STARTED;
  private Object lock = new Object();

  /**
   * Marks the current state to be STARTED.
   * <p>
   * REQUIRES: Current state is NOT_STARTED.
   */
  public void start() {
    synchronized (lock) {
      Preconditions.checkState(currentState == CurrentState.NOT_STARTED,
          "Cannot start: %s", currentState);
      currentState = CurrentState.STARTED;
    }
  }

  /**
   * Marks the current state to be STOPPED.
   * <p>
   * REQUIRES: Current state is STARTED.
   */
  public void stop() {
    synchronized (lock) {
      Preconditions.checkState(currentState == CurrentState.STARTED,
          "Cannot stop: %s", currentState);
      currentState = CurrentState.STOPPED;
    }
  }

  /**
   * Returns true iff {@link #start} has been called on this but {@link #stop} has not been called.
   */
  public boolean isStarted() {
    synchronized (lock) {
      return currentState == CurrentState.STARTED;
    }
  }

  /** Returns true iff {@link #start} and {@link #stop} have been called on this object. */
  public boolean isStopped() {
    synchronized (lock) {
      return currentState == CurrentState.STOPPED;
    }
  }
}
