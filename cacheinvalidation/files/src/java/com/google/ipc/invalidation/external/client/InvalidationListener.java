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
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.external.client.types.Invalidation;
import com.google.ipc.invalidation.external.client.types.ObjectId;

/**
 * Interface through which invalidation-related events are delivered by the library to the
 * application. Each event must be acknowledged by the application. Each includes an AckHandle that
 * the application must use to call {@code InvalidationClient.acknowledge} after it is done handling
 * that event.
 *
 */
public interface InvalidationListener {
  /** Possible registration states for an object. */
  public enum RegistrationState {
    REGISTERED,
    UNREGISTERED
  }

  /**
   * Called in response to the {@code InvalidationClient.start} call. Indicates that the
   * InvalidationClient is now ready for use, i.e., calls such as register/unregister can be
   * performed on that object.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   */
  void ready(InvalidationClient client);

  /**
   * Indicates that an object has been updated to a particular version.
   *
   * The Ticl guarantees that this callback will be invoked at least once for
   * every invalidation that it guaranteed to deliver. It does not guarantee
   * exactly-once delivery or in-order delivery (with respect to the version
   * number).
   *
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckHandle)} with the provided {@code
   * ackHandle} otherwise the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param ackHandle event acknowledgement handle
   */
  void invalidate(InvalidationClient client, Invalidation invalidation, AckHandle ackHandle);

  /**
   * As {@link #invalidate}, but for an unknown application store version. The object may or may not
   * have been updated - to ensure that the application does not miss an update from its backend,
   * the application must check and/or fetch the latest version from its store.
   */
  void invalidateUnknownVersion(InvalidationClient client, ObjectId objectId,
      AckHandle ackHandle);

  /**
   * Indicates that the application should consider all objects to have changed.
   * This event is generally sent when the client has been disconnected from the
   * network for too long a period and has been unable to resynchronize with the
   * update stream, but it may be invoked arbitrarily (although  tries hard
   * not to invoke it under normal circumstances).
   *
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckHandle)} with the provided {@code
   * ackhandle} otherwise the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param ackHandle event acknowledgement handle
   */
  void invalidateAll(InvalidationClient client, AckHandle ackHandle);

  /**
   * Indicates that the registration state of an object has changed.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param objectId the id of the object whose state changed
   * @param regState the new state
   */
  void informRegistrationStatus(InvalidationClient client, ObjectId objectId,
      RegistrationState regState);

  /**
   * Indicates that an object registration or unregistration operation may have failed.
   * <p>
   * For transient failures, the application can retry the registration later - if it chooses to do
   * so, it must use a sensible backoff policy such as exponential backoff. For permanent failures,
   * it must not automatically retry without fixing the situation (e.g., by presenting a dialog box
   * to the user).
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param objectId the id of the object whose state changed
   * @param isTransient whether the error is transient or permanent
   * @param errorMessage extra information about the message
   */
  void informRegistrationFailure(InvalidationClient client, ObjectId objectId,
      boolean isTransient, String errorMessage);

  /**
   * Indicates that the all registrations for the client are in an unknown state (e.g., they could
   * have been removed). The application MUST inform the {@code InvalidationClient} of its
   * registrations once it receives this event.  The requested objects are those for which the
   * digest of their serialized object ids matches a particular prefix bit-pattern. The digest for
   * an object id is computed as following (the digest chosen for this method is SHA-1):
   * <p>
   *   digest = new Digest();
   *   digest.update(Little endian encoding of object source type)
   *   digest.update(object name)
   *   digest.getDigestSummary()
   * <p>
   * For a set of objects, digest is computed by sorting lexicographically based on their digests
   * and then performing the update process given above (i.e., calling digest.update on each
   * object's digest and then calling getDigestSummary at the end).
   * <p>
   * IMPORTANT: A client can always register for more objects than what is requested here. For
   * example, in response to this call, the client can ignore the prefix parameters and register for
   * all its objects.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param prefix prefix of the object ids as described above.
   * @param prefixLength number of bits in {@code prefix} to consider.
   */
  void reissueRegistrations(InvalidationClient client, byte[] prefix, int prefixLength);

  /**
   * Informs the listener about errors that have occurred in the backend, e.g., authentication,
   * authorization problems.
   * <p>
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckHandle)} with the provided {@code ackHandle} otherwise
   * the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param errorInfo information about the error
   */
  void informError(InvalidationClient client, ErrorInfo errorInfo);
}
