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

import com.google.ipc.invalidation.common.CommonProtoStrings2;
import com.google.ipc.invalidation.common.CommonProtos2;
import com.google.ipc.invalidation.common.DigestFunction;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.ticl.Statistics.ClientErrorType;
import com.google.ipc.invalidation.ticl.TestableInvalidationClient.RegistrationManagerState;
import com.google.ipc.invalidation.util.InternalBase;
import com.google.ipc.invalidation.util.TextBuilder;
import com.google.ipc.invalidation.util.TypedUtil;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationStatus;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSubtree;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSummary;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;


/**
 * Object to track desired client registrations. This class belongs to caller (e.g.,
 * InvalidationClientImpl) and is not thread-safe - the caller has to use this class in a
 * thread-safe manner.
 *
 */
class RegistrationManager extends InternalBase {

  /** Prefix used to request all registrations. */
  static final byte[] EMPTY_PREFIX = new byte[]{};

  /** The set of regisrations that the application has requested for. */
  private DigestStore<ObjectIdP> desiredRegistrations;

  /** Statistics objects to track number of sent messages, etc. */
  private final Statistics statistics;

  /** Latest known server registration state summary. */
  private ProtoWrapper<RegistrationSummary> lastKnownServerSummary;

  private final Logger logger;

  RegistrationManager(Logger logger, Statistics statistics, DigestFunction digestFunction) {
    this.logger = logger;
    this.statistics = statistics;
    this.desiredRegistrations = new SimpleRegistrationStore(digestFunction);

    // Initialize the server summary with a 0 size and the digest corresponding
    // to it.  Using defaultInstance would wrong since the server digest will
    // not match unnecessarily and result in an info message being sent.
    this.lastKnownServerSummary = ProtoWrapper.of(getRegistrationSummary());
  }

  /**
   * Returns a copy of the registration manager's state
   * <p>
   * Direct test code MUST not call this method on a random thread. It must be called on the
   * InvalidationClientImpl's internal thread.
   */
  
  RegistrationManagerState getRegistrationManagerStateCopyForTest(DigestFunction digestFunction) {
    List<ObjectIdP> registeredObjects = new ArrayList<ObjectIdP>();
    for (ObjectIdP oid : desiredRegistrations.getElements(EMPTY_PREFIX, 0)) {
      registeredObjects.add(oid);
    }
    return new RegistrationManagerState(
        RegistrationSummary.newBuilder(getRegistrationSummary()).build(),
        RegistrationSummary.newBuilder(lastKnownServerSummary.getProto()).build(),
        registeredObjects);
  }

  /**
   * Sets the digest store to be {@code digestStore} for testing purposes.
   * <p>
   * REQUIRES: This method is called before the Ticl has done any operations on this object.
   */
  
  void setDigestStoreForTest(DigestStore<ObjectIdP> digestStore) {
    this.desiredRegistrations = digestStore;
    this.lastKnownServerSummary = ProtoWrapper.of(getRegistrationSummary());
  }

  
  Collection<ObjectIdP> getRegisteredObjectsForTest() {
    return desiredRegistrations.getElements(EMPTY_PREFIX, 0);
  }

  /** Perform registration/unregistation for all objects in {@code objectIds}. */
  Collection<ObjectIdP> performOperations(Collection<ObjectIdP> objectIds,
      RegistrationP.OpType regOpType) {
    if (regOpType == RegistrationP.OpType.REGISTER) {
      return desiredRegistrations.add(objectIds);
    } else {
      return desiredRegistrations.remove(objectIds);
    }
  }

  /**
   * Returns a registration subtree for registrations where the digest of the object id begins with
   * the prefix {@code digestPrefix} of {@code prefixLen} bits. This method may also return objects
   * whose digest prefix does not match {@code digestPrefix}.
   */
  RegistrationSubtree getRegistrations(byte[] digestPrefix, int prefixLen) {
    RegistrationSubtree.Builder builder = RegistrationSubtree.newBuilder();
    for (ObjectIdP objectId : desiredRegistrations.getElements(digestPrefix, prefixLen)) {
      builder.addRegisteredObject(objectId);
    }
    return builder.build();
  }

  /**
   * Handles registration operation statuses from the server. Returns a list of booleans, one per
   * registration status that indicates if the registration manager considered the registration
   * operation to be successful or not (e.g., if the object was registered and the server
   * sent back a reply of successful unregistration, the registration manager will consider that
   * as failure since the application's intent is to register that object).
   */
  List<Boolean> handleRegistrationStatus(List<RegistrationStatus> registrationStatuses) {

    // Local-processing result code for each element of registrationStatuses. Indicates whether
    // the registration status was compatible with the client's desired state (e.g., a successful
    // unregister from the server when we desire a registration is incompatible).
    List<Boolean> successStatus = new ArrayList<Boolean>(registrationStatuses.size());
    for (RegistrationStatus registrationStatus : registrationStatuses) {
      ObjectIdP objectIdProto = registrationStatus.getRegistration().getObjectId();

      // We start off with the local-processing set as success, then potentially fail.
      boolean isSuccess = true;

      // if the server operation succeeded, then local processing fails on "incompatibility" as
      // defined above.
      if (CommonProtos2.isSuccess(registrationStatus.getStatus())) {
        boolean inRequestedMap = desiredRegistrations.contains(objectIdProto);
        boolean isRegister =
            registrationStatus.getRegistration().getOpType() == RegistrationP.OpType.REGISTER;
        boolean discrepancyExists = isRegister ^ inRequestedMap;
        if (discrepancyExists) {
          // Just remove the registration and issue registration failure.
          // Caller must issue registration failure to the app so that we find out the actual state
          // of the registration.
          desiredRegistrations.remove(objectIdProto);
          statistics.recordError(ClientErrorType.REGISTRATION_DISCREPANCY);
          logger.info("Ticl discrepancy detected: registered = %s, requested = %s. " +
              "Removing %s from requested",
              isRegister, inRequestedMap, CommonProtoStrings2.toLazyCompactString(objectIdProto));
          isSuccess = false;
        }
      } else {
        // If the server operation failed, then local processing fails.
        desiredRegistrations.remove(objectIdProto);
        logger.fine("Removing %s from committed",
            CommonProtoStrings2.toLazyCompactString(objectIdProto));
        isSuccess = false;
      }
      successStatus.add(isSuccess);
    }
    return successStatus;
  }

  /** Removes all the registrations in this manager and returns the list. */
  Collection<ObjectIdP> removeRegisteredObjects() {
    return desiredRegistrations.removeAll();
  }

  //
  // Digest-related methods
  //

  /** Returns a summary of the desired registrations. */
  RegistrationSummary getRegistrationSummary() {
    return CommonProtos2.newRegistrationSummary(desiredRegistrations.size(),
        desiredRegistrations.getDigest());
  }

  /** Informs the manager of a new registration state summary from the server. */
  void informServerRegistrationSummary(RegistrationSummary regSummary) {
    if (regSummary != null) {
      this.lastKnownServerSummary = ProtoWrapper.of(regSummary);
    }
  }

  /**
   * Returns whether the local registration state and server state agree, based on the last
   * received server summary (from {@link #informServerRegistrationSummary}).
   */
  boolean isStateInSyncWithServer() {
    return TypedUtil.equals(lastKnownServerSummary, ProtoWrapper.of(getRegistrationSummary()));
  }

  @Override
  public void toCompactString(TextBuilder builder) {
    builder.appendFormat("Last known digest: %s, Requested regs: %s", lastKnownServerSummary,
        desiredRegistrations);
  }
}
