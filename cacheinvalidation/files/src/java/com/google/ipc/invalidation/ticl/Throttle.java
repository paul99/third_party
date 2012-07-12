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
import com.google.ipc.invalidation.external.client.SystemResources.Scheduler;
import com.google.ipc.invalidation.util.NamedRunnable;
import com.google.protos.ipc.invalidation.ClientProtocol.RateLimitP;

import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedList;
import java.util.List;

/**
 * An abstraction for rate-limiting the calls to a particular function over
 * multiple window sizes.
 *
 */
public class Throttle {

  // High-level design: maintains a list of recent times at which the throttled function was called.
  // When a call is made to fire(), checks, for each rate limit, that calling the function now would
  // not violate the limit.  If it would violate a limit, schedules a task to retry at the soonest
  // point in the future at which the given limit would not be violated.  (If such a task is already
  // scheduled, simply returns and lets the existing task retry.)  Otherwise, if no limits would be
  // violated, allows the call and records the current time.

  /** Rate limits to be enforced by this throttler. */
  private final List<RateLimitP> rateLimits;

  /** Scheduling for running deferred tasks. */
  private final Scheduler scheduler;

  /** The listener whose calls are to be throttled. */
  private final Runnable listener;

  /**
   * Whether we've scheduled a task to retry a call to {@code fire} because
   * a previous call would have violated a rate limit.
   */
  private boolean timerScheduled = false;

  /**
   * A list of recent times at which we've called the listener -- large enough
   * to verify that all of the rate limits are being observed.
   */
  private final LinkedList<Long> recentEventTimes;

  /** The maximum number of recent event times we need to remember. */
  private final int maxRecentEvents;

  public Throttle(final List<RateLimitP> rateLimits, Scheduler scheduler,
      Runnable listener) {
    this.rateLimits = rateLimits;
    for (RateLimitP rateLimit : rateLimits) {
      Preconditions.checkArgument(rateLimit.getWindowMs() > rateLimit.getCount(),
          "rate limit window must exceed event count: {0} vs {1}", rateLimit.getWindowMs(),
          rateLimit.getCount());
    }
    this.scheduler = scheduler;
    this.listener = listener;
    this.recentEventTimes = new LinkedList<Long>();
    final Comparator<RateLimitP> comparator = new Comparator<RateLimitP>() {
      @Override
      public int compare(RateLimitP o1, RateLimitP o2) {
        // Compare just based on the count.
        return o1.getCount() - o2.getCount();
      }
    };
    this.maxRecentEvents = rateLimits.isEmpty() ?
        0 : Collections.max(rateLimits, comparator).getCount();
  }

  /**
   * If calling the listener would not violate the rate limits, does so.
   * Otherwise, schedules a timer to do so as soon as doing so would not violate
   * the rate limits, unless such a timer is already set, in which case does
   * nothing.  I.e., once a rate limit is reached, subsequent calls are dropped
   * and never result in additional calls to the listener.
   */
  public void fire() {
    if (timerScheduled) {
      // We're already rate-limited and have a deferred call scheduled.  Just
      // return.  The flag will be reset when the deferred task runs.
      return;
    }
    // Go through all of the limits to see if we've hit one.  If so, schedule a
    // task to try again once that limit won't be violated.  If no limits would be
    // violated, send.
    long now = scheduler.getCurrentTimeMs();
    for (RateLimitP rateLimit : rateLimits) {
      // We're now checking whether sending would violate a rate limit of 'count'
      // messages per 'window_size'.
      int count = rateLimit.getCount();
      int windowMs = rateLimit.getWindowMs();

      // First, see how many messages we've sent so far (up to the size of our
      // recent message buffer).
      int numRecentMessages = recentEventTimes.size();

      // Check whether we've sent enough messages yet that we even need to
      // consider this rate limit.
      if (numRecentMessages >= count) {
        // If we've sent enough messages to reach this limit, see how long ago we
        // sent the first message in the interval, and add sufficient delay to
        // avoid violating the rate limit.

        // We have sent at least 'count' messages.  See how long ago we sent the
        // 'count'-th last message.  This defines the start of a window in which
        // no more than 'count' messages may be sent.
        long windowStart = recentEventTimes.get(numRecentMessages - count);

        // The end of this window is 'window_size' after the start.
        long windowEnd = windowStart + windowMs;

        // Check where the end of the window is relative to the current time.  If
        // the end of the window is in the future, then sending now would violate
        // the rate limit, so we must defer.
        long windowEndFromNow = windowEnd - now;
        if (windowEndFromNow > 0) {
          // Rate limit would be violated, so schedule a task to try again.

          // Set the flag to indicate we have a deferred task scheduled.  No need
          // to continue checking other rate limits now.
          timerScheduled = true;
          scheduler.schedule((int) windowEndFromNow, new NamedRunnable("Throttle.fire") {
            @Override
            public void run() {
              timerScheduled = false;
              fire();
            }
          });
          return;
        }
      }
    }
    // We checked all the rate limits, and none would have been violated, so it's
    // safe to call the listener.
    scheduler.schedule(Scheduler.NO_DELAY, listener);

    // Record the fact that we're triggering an event now.
    recentEventTimes.add(scheduler.getCurrentTimeMs());

    // Only save up to maxRecentEvents event times.
    if (recentEventTimes.size() > maxRecentEvents) {
      recentEventTimes.remove();
    }
  }
}
