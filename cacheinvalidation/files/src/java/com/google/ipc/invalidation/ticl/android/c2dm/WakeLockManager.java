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

package com.google.ipc.invalidation.ticl.android.c2dm;


import com.google.common.base.Preconditions;

import android.content.Context;
import android.os.PowerManager;

import java.util.HashMap;
import java.util.Map;

/**
 * Singleton that manages wake locks identified by a key. Wake locks are refcounted so if they are
 * acquired multiple times with the same key they will not unlocked until they are released an
 * equivalent number of times.
 */
public class WakeLockManager {

  private static final String TAG = "WakeLockManager";

  private static final Object LOCK = new Object();

  private static WakeLockManager theManager;

  private final Map<Object, PowerManager.WakeLock> wakeLocks;

  private PowerManager powerManager;

  private WakeLockManager(Context context) {
    powerManager = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
    wakeLocks = new HashMap<Object, PowerManager.WakeLock>();
  }

  /**
   * Returns the wake lock manager.
   */
  public static WakeLockManager getInstance(Context context) {
    synchronized (LOCK) {
      if (theManager == null) {
        theManager = new WakeLockManager(context);
      }
      return theManager;
    }
  }

  /**
   * Acquires a wake lock identified by the key.
   */
  public void acquire(Object key) {
    Preconditions.checkNotNull(key, "Key can not be null");
    log(key, "acquiring");
    getWakeLock(key).acquire();
  }

  /**
   * Releases the wake lock identified by the key.
   */
  public void release(Object key) {
    Preconditions.checkNotNull(key, "Key can not be null");
    synchronized (LOCK) {
      PowerManager.WakeLock wakelock = getWakeLock(key);
      wakelock.release();
      log(key, "released");
      if (!wakelock.isHeld()) {
        wakeLocks.remove(key);
        log(key, "freed");
      }
    }
  }

  /**
   * Returns true if there is currently a wake lock held for the provided key.
   */
  public boolean isHeld(Object key) {
    Preconditions.checkNotNull(key, "Key can not be null");
    synchronized (LOCK) {
      if (!wakeLocks.containsKey(key)) {
        return false;
      }
      return getWakeLock(key).isHeld();
    }
  }

  
  boolean hasWakeLocks() {
    return !wakeLocks.isEmpty();
  }

  
  void resetForTest() {
    wakeLocks.clear();
  }

  private PowerManager.WakeLock getWakeLock(Object key) {
    if (key == null) {
      throw new IllegalArgumentException("Key can not be null");
    }
    synchronized (LOCK) {
      PowerManager.WakeLock wakeLock = wakeLocks.get(key);
      if (wakeLock == null) {
        wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, key.toString());
        wakeLocks.put(key, wakeLock);
      }
      return wakeLock;
    }
  }

  private static void log(Object key, String action) {
    // TODO(kmarvin) Reenable when there is a model for verbose dev mode logging
    //Log.v(TAG, " WakeLock " + action + " for key: {" + key + "}");
  }
}
