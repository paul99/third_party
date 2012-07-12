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

package com.google.ipc.invalidation.external.client;

import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ObjectId;

import java.util.Collection;

/**
 * Interface for the invalidation client library.
 *
 */
public interface InvalidationClient {

  /**
   * Starts the client. This method must be called before any other method is invoked. The client is
   * considered to be started after {@code InvalidationListener.ready} has received by the
   * application.
   * <p>
   * REQUIRES: {@link #start} has not already been called.
   * Also, the resources given to the client must have been started by the caller.
   */
  void start();

  /**
   * Stops the client. After this method has been called, it is an error to call any other method.
   * Does not stop the resources bound to this client.
   * <p>
   * REQUIRES: {@link #start} has already been called.
   */
  void stop();

  /**
   * Requests that the Ticl register to receive notifications for the object with id
   * {@code objectId}. The library guarantees that the caller will be informed of the results of
   * this call either via {@code InvalidationListener.informRegistrationStatus} or
   * {@code InvalidationListener.informRegistrationFailure} unless the library informs the caller of
   * a connection failure via {@code InvalidationListener.informError}. The caller should consider
   * the registration to have succeeded only if it gets a call
   * {@code InvalidationListener.informRegistrationStatus} for {@code objectId} with
   * {@code InvalidationListener.RegistrationState.REGISTERED}.
   * Note that if the network is disconnected, the listener events will probably show up when the
   * network connection is repaired.
   * <p>
   * REQUIRES: {@link #start} has been called and and {@code InvalidationListener.ready} has been
   * received by the application's listener.
   */
  void register(ObjectId objectId);

  /**
   * Registrations for multiple objects. See the specs on {@link #register(ObjectId)} for more
   * details. If the caller needs to register for a number of object ids, this method is more
   * efficient than calling {@code register} in a loop.
   */
  void register(Collection<ObjectId> objectIds);

  /**
   * Requests that the Ticl unregister for notifications for the object with id {@code objectId}.
   * The library guarantees that the caller will be informed of the results of this call either via
   * {@code InvalidationListener.informRegistrationStatus} or
   * {@code InvalidationListener.informRegistrationFailure} unless the library informs the caller of
   * a connection failure via {@code InvalidationListener.informError}. The caller should consider
   * the unregistration to have succeeded only if it gets a call
   * {@code InvalidationListener.informRegistrationStatus} for {@code objectId} with
   * {@code InvalidationListener.RegistrationState.UNREGISTERED}.
   * Note that if the network is disconnected, the listener events will probably show up when the
   * network connection is repaired.
   * <p>
   * REQUIRES: {@link #start} has been called and and {@code InvalidationListener.ready} has been
   * receiveed by the application's listener.
   */
  void unregister(ObjectId objectId);

  /**
   * Unregistrations for multiple objects. See the specs on {@link #unregister(ObjectId)} for more
   * details. If the caller needs to unregister for a number of object ids, this method is more
   * efficient than calling {@code unregister} in a loop.
   */
  void unregister(Collection<ObjectId> objectIds);

  /**
   * Acknowledges the {@link InvalidationListener} event that was delivered with the provided
   * acknowledgement handle. This indicates that the client has accepted responsibility for
   * processing the event and it does not need to be redelivered later.
   * <p>
   * REQUIRES: {@link #start} has been called and and {@code InvalidationListener.ready} has been
   * received by the application's listener.
   */
  void acknowledge(AckHandle ackHandle);
}
