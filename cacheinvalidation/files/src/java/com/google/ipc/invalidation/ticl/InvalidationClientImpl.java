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

import static com.google.ipc.invalidation.external.client.SystemResources.Scheduler.NO_DELAY;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.common.CommonInvalidationConstants2;
import com.google.ipc.invalidation.common.CommonProtoStrings2;
import com.google.ipc.invalidation.common.CommonProtos2;
import com.google.ipc.invalidation.common.DigestFunction;
import com.google.ipc.invalidation.common.ObjectIdDigestUtils;
import com.google.ipc.invalidation.common.TiclMessageValidator2;
import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.SystemResources.NetworkChannel;
import com.google.ipc.invalidation.external.client.SystemResources.Scheduler;
import com.google.ipc.invalidation.external.client.SystemResources.Storage;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.external.client.types.Invalidation;
import com.google.ipc.invalidation.external.client.types.ObjectId;
import com.google.ipc.invalidation.external.client.types.SimplePair;
import com.google.ipc.invalidation.external.client.types.Status;
import com.google.ipc.invalidation.ticl.ProtocolHandler.ProtocolListener;
import com.google.ipc.invalidation.ticl.ProtocolHandler.ServerMessageHeader;
import com.google.ipc.invalidation.ticl.Statistics.ClientErrorType;
import com.google.ipc.invalidation.ticl.Statistics.IncomingOperationType;
import com.google.ipc.invalidation.util.Box;
import com.google.ipc.invalidation.util.Bytes;
import com.google.ipc.invalidation.util.ExponentialBackoffDelayGenerator;
import com.google.ipc.invalidation.util.InternalBase;
import com.google.ipc.invalidation.util.NamedRunnable;
import com.google.ipc.invalidation.util.Smearer;
import com.google.ipc.invalidation.util.TextBuilder;
import com.google.ipc.invalidation.util.TypedUtil;
import com.google.protobuf.ByteString;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protos.ipc.invalidation.Channel.NetworkEndpointId;
import com.google.protos.ipc.invalidation.Client.AckHandleP;
import com.google.protos.ipc.invalidation.Client.PersistentTiclState;
import com.google.protos.ipc.invalidation.ClientProtocol.ApplicationClientIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientConfigP;
import com.google.protos.ipc.invalidation.ClientProtocol.ErrorMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.InfoRequestMessage.InfoType;
import com.google.protos.ipc.invalidation.ClientProtocol.InvalidationP;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationStatus;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSubtree;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSummary;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Random;


/**
 * Implementation of the  Invalidation Client Library (Ticl).
 *
 */
public class InvalidationClientImpl extends InternalBase
    implements TestableInvalidationClient, ProtocolListener {

  /** A task for acquiring tokens from the server. */
  private class AcquireTokenTask extends RecurringTask {
    AcquireTokenTask() {
      super("AcquireToken", internalScheduler, logger, smearer,
          createExpBackOffGenerator(config.getNetworkTimeoutDelayMs()), NO_DELAY,
          config.getNetworkTimeoutDelayMs());
    }

    @Override
    public boolean runTask() {
      // If token is still not assigned (as expected), sends a request. Otherwise, ignore.
      if (clientToken == null) {
        // Allocate a nonce and send a message requesting a new token.
        setNonce(ByteString.copyFromUtf8(Long.toString(
            internalScheduler.getCurrentTimeMs())));
        protocolHandler.sendInitializeMessage(applicationClientId, nonce, "AcquireToken");
        return true;  // Reschedule to check state, retry if necessary after timeout.
      } else {
        return false;  // Don't reschedule.
      }
    }
  }

  /**
   * A task that schedules heartbeats when the registration summary at the client is not
   * in sync with the registration summary from the server.
   */
  private class RegSyncHeartbeatTask extends RecurringTask {
    RegSyncHeartbeatTask() {
      super("RegSyncHeartbeat", internalScheduler, logger, smearer,
          createExpBackOffGenerator(config.getNetworkTimeoutDelayMs()),
          config.getNetworkTimeoutDelayMs(), config.getNetworkTimeoutDelayMs());
    }

    @Override
    public boolean runTask() {
      if (!registrationManager.isStateInSyncWithServer()) {
        // Simply send an info message to ensure syncing happens.
        logger.info("Registration state not in sync with server: %s", registrationManager);
        sendInfoMessageToServer(false, true /* request server summary */);
        return true;
      } else {
        logger.info("Not sending message since state is now in sync");
        return false;
      }
    }
  }

  /** A task that writes the token to persistent storage. */
  private class PersistentWriteTask extends RecurringTask {
    /*
     * This class implements a "train" of events that attempt to reliably write state to
     * storage. The train continues until runTask encounters a termination condition, in
     * which the state currently in memory and the state currently in storage match.
     */
    /** The last client token that was written to to persistent state successfully. */
    private final Box<ProtoWrapper<PersistentTiclState>> lastWrittenState =
        Box.of(ProtoWrapper.of(PersistentTiclState.getDefaultInstance()));

    PersistentWriteTask() {
      super("PersistentWrite", internalScheduler,
        logger, smearer, createExpBackOffGenerator(config.getWriteRetryDelayMs()), NO_DELAY,
        config.getWriteRetryDelayMs());
    }

    @Override
    public boolean runTask() {
      if (clientToken == null) {
        // We cannot write without a token. We must do this check before creating the
        // PersistentTiclState because newPersistentTiclState cannot handle null tokens.
        return false;
      }

      // Compute the state that we will write if we decide to go ahead with the write.
      final ProtoWrapper<PersistentTiclState> state =
          ProtoWrapper.of(CommonProtos2.newPersistentTiclState(clientToken, lastMessageSendTimeMs));
      byte[] serializedState = PersistenceUtils.serializeState(state.getProto(), digestFn);

      // Decide whether or not to do the write. The decision varies depending on whether or
      // not the channel supports offline delivery. If we decide not to do the write, then
      // that means the in-memory and stored state match semantically, and the train stops.
      if (config.getChannelSupportsOfflineDelivery()) {
        // For offline delivery, we want the entire state to match, since we write the last
        // send time for every message.
        if (state.equals(lastWrittenState.get())) {
          return false;
        }
      } else {
        // If we do not support offline delivery, we avoid writing the state on each message, and
        // we avoid checking the last-sent time (we check only the client token).
        if (state.getProto().getClientToken().equals(
                lastWrittenState.get().getProto().getClientToken())) {
          return false;
        }
      }

      // We decided to do the write.
      storage.writeKey(CLIENT_TOKEN_KEY, serializedState, new Callback<Status>() {
        @Override
        public void accept(Status status) {
          logger.info("Write state completed: %s for %s", status, state.getProto());
          if (status.isSuccess()) {
            // Set lastWrittenToken to be the token that was written (NOT clientToken - which
            // could have changed while the write was happening).
            lastWrittenState.set(state);
          } else {
            statistics.recordError(ClientErrorType.PERSISTENT_WRITE_FAILURE);
          }
        }
      });
      return true;  // Reschedule after timeout to make sure that write does happen.
    }
  }

  /** A task for sending heartbeats to the server. */
  private class HeartbeatTask extends RecurringTask {

    /** Next time that the performance counters are sent to the server. */
    private long nextPerformanceSendTimeMs;

    HeartbeatTask() {
      super("Heartbeat", internalScheduler, logger, smearer, null, config.getHeartbeatIntervalMs(),
          NO_DELAY);
      this.nextPerformanceSendTimeMs = internalScheduler.getCurrentTimeMs() +
          smearer.getSmearedDelay(config.getPerfCounterDelayMs());
    }

    @Override
    public boolean runTask() {
      // Send info message. If needed, send performance counters and reset the next performance
      // counter send time.
      logger.info("Sending heartbeat to server: %s", this);
      boolean mustSendPerfCounters =
          nextPerformanceSendTimeMs > internalScheduler.getCurrentTimeMs();
      if (mustSendPerfCounters) {
        this.nextPerformanceSendTimeMs = internalScheduler.getCurrentTimeMs() +
            getSmearer().getSmearedDelay(config.getPerfCounterDelayMs());
      }
      sendInfoMessageToServer(mustSendPerfCounters, !registrationManager.isStateInSyncWithServer());
      return true;  // Reschedule.
    }
  }

  //
  // End of nested classes.
  //

  /** The single key used to write all the Ticl state. */
  
  public static final String CLIENT_TOKEN_KEY = "ClientToken";

  /** Resources for the Ticl. */
  private final SystemResources resources;

  /**
   * Reference into the resources object for cleaner code. All Ticl code must be scheduled on this
   * scheduler.
   */
  private final Scheduler internalScheduler;

  /** Logger reference into the resources object for cleaner code. */
  private final Logger logger;

  /** A storage layer which schedules the callbacks on the internal scheduler thread. */
  private final SafeStorage storage;

  /** Application callback interface. */
  private final CheckingInvalidationListener listener;

  /** Configuration for this instance. */
  private ClientConfigP config;

  /** Application identifier for this client. */
  private final ApplicationClientIdP applicationClientId;

  /** Object maintaining the registration state for this client. */
  private final RegistrationManager registrationManager;

  /** Object handling low-level wire format interactions. */
  private final ProtocolHandler protocolHandler;

  /** Used to validate messages */
  private final TiclMessageValidator2 msgValidator;

  /** The function for computing the registration and persistence state digests. */
  private final DigestFunction digestFn = new ObjectIdDigestUtils.Sha1DigestFunction();

  /** The state of the Ticl whether it has started or not. */
  private final RunState ticlState = new RunState();

  /** Statistics objects to track number of sent messages, etc. */
  private final Statistics statistics = new Statistics();

  /** A smearer to make sure that delays are randomized a little bit. */
  private final Smearer smearer;

  /** Current client token known from the server. */
  private ByteString clientToken = null;

  // After the client starts, exactly one of nonce and clientToken is non-null.

  /** If not {@code null}, nonce for pending identifier request. */
  private ByteString nonce = null;

  /** Whether we should send registrations to the server or not. */
  // TODO: Make the server summary in the registration manager nullable
  // and replace this variable with a test for whether it's null or not.
  private boolean shouldSendRegistrations;

  /** A random number generator. */
  private final Random random;

  /** Last time a message was sent to the server. */
  private long lastMessageSendTimeMs = 0;

  /** A task for acquiring the token (if the client has no token). */
  private AcquireTokenTask acquireTokenTask;

  /** Task for checking if reg summary is out of sync and then sending a heartbeat to the server. */
  private RegSyncHeartbeatTask regSyncHeartbeatTask;

  /** Task for writing the state blob to persistent storage. */
  private PersistentWriteTask persistentWriteTask;

  /** A task for periodic heartbeats. */
  private HeartbeatTask heartbeatTask;

  /**
   * Constructs a client.
   *
   * @param resources resources to use during execution
   * @param random a random number generator
   * @param clientType client type code
   * @param clientName application identifier for the client
   * @param config configuration for the client
   * @param applicationName name of the application using the library (for debugging/monitoring)
   * @param listener application callback
   */
  public InvalidationClientImpl(final SystemResources resources, Random random, int clientType,
      final byte[] clientName, ClientConfigP config, String applicationName,
      InvalidationListener listener) {
    this.resources = resources;
    this.random = random;
    this.logger = resources.getLogger();
    this.internalScheduler = resources.getInternalScheduler();
    this.storage = new SafeStorage(resources.getStorage());
    storage.setSystemResources(resources);
    this.config = config;
    this.registrationManager = new RegistrationManager(logger, statistics, digestFn);
    this.smearer = new Smearer(random, this.config.getSmearPercent());
    this.applicationClientId =
        CommonProtos2.newApplicationClientIdP(clientType, ByteString.copyFrom(clientName));
    this.listener = new CheckingInvalidationListener(listener, statistics, internalScheduler,
        resources.getListenerScheduler(), logger);
    this.msgValidator = new TiclMessageValidator2(resources.getLogger());

    // Creates the tasks used by the Ticl for token acquisition, heartbeats, persistent writes and
    // registration sync.
    createSchedulingTasks();

    this.protocolHandler = new ProtocolHandler(config.getProtocolHandlerConfig(), resources,
        smearer, statistics, applicationName, this, msgValidator);
    logger.info("Created client: %s", this);
  }

  /** Returns a default config builder for the client. */
  public static ClientConfigP.Builder createConfig() {
    return ClientConfigP.newBuilder()
        .setVersion(CommonProtos2.newVersion(CommonInvalidationConstants2.CONFIG_MAJOR_VERSION,
            CommonInvalidationConstants2.CONFIG_MINOR_VERSION))
        .setProtocolHandlerConfig(ProtocolHandler.createConfig());
  }

  /** Returns a configuration builder with parameters set for unit tests. */
  public static ClientConfigP.Builder createConfigForTest() {
    return ClientConfigP.newBuilder()
        .setVersion(CommonProtos2.newVersion(CommonInvalidationConstants2.CONFIG_MAJOR_VERSION,
            CommonInvalidationConstants2.CONFIG_MINOR_VERSION))
        .setProtocolHandlerConfig(ProtocolHandler.createConfigForTest())
        .setNetworkTimeoutDelayMs(2 * 1000)
        .setHeartbeatIntervalMs(5 * 1000)
        .setWriteRetryDelayMs(500);
  }

  /**
   * Creates the tasks used by the Ticl for token acquisition, heartbeats, persistent writes and
   * registration sync.
   */
  private void createSchedulingTasks() {
    this.acquireTokenTask = new AcquireTokenTask();
    this.heartbeatTask = new HeartbeatTask();
    this.regSyncHeartbeatTask = new RegSyncHeartbeatTask();
    this.persistentWriteTask = new PersistentWriteTask();
  }

   // Methods for TestableInvalidationClient.

  @Override
  
  public ClientConfigP getConfigForTest() {
    return this.config;
  }

  @Override
  
  public byte[] getApplicationClientIdForTest() {
    return applicationClientId.toByteArray();
  }

  @Override
  
  public InvalidationListener getInvalidationListenerForTest() {
    return this.listener.getDelegate();
  }

  
  public SystemResources getResourcesForTest() {
    return resources;
  }

  @Override
  
  public Statistics getStatisticsForTest() {
    Preconditions.checkState(resources.getInternalScheduler().isRunningOnThread());
    return statistics;
  }

  @Override
  
  public DigestFunction getDigestFunctionForTest() {
    return this.digestFn;
  }

  @Override
  
  public long getNextMessageSendTimeMsForTest() {
    Preconditions.checkState(resources.getInternalScheduler().isRunningOnThread());
    return protocolHandler.getNextMessageSendTimeMsForTest();
  }

  @Override
  
  public RegistrationManagerState getRegistrationManagerStateCopyForTest() {
    Preconditions.checkState(resources.getInternalScheduler().isRunningOnThread());
    return registrationManager.getRegistrationManagerStateCopyForTest(
        new ObjectIdDigestUtils.Sha1DigestFunction());
  }

  @Override
  
  public void changeNetworkTimeoutDelayForTest(int networkTimeoutDelayMs) {
    config = ClientConfigP.newBuilder(config).setNetworkTimeoutDelayMs(networkTimeoutDelayMs)
        .build();
    createSchedulingTasks();
  }

  @Override
  
  public void changeHeartbeatDelayForTest(int heartbeatDelayMs) {
    config = ClientConfigP.newBuilder(config).setHeartbeatIntervalMs(heartbeatDelayMs).build();
    createSchedulingTasks();
  }

  @Override
  
  public void setDigestStoreForTest(DigestStore<ObjectIdP> digestStore) {
    Preconditions.checkState(!resources.isStarted());
    registrationManager.setDigestStoreForTest(digestStore);
  }

  @Override
  
  public ByteString getClientTokenForTest() {
    return getClientToken();
  }

  @Override
  
  public String getClientTokenKeyForTest() {
    return CLIENT_TOKEN_KEY;
  }

  @Override
  public boolean isStartedForTest() {
    return ticlState.isStarted();
  }

  @Override
  public void stopResources() {
    resources.stop();
  }

  @Override
  public long getResourcesTimeMs() {
    return resources.getInternalScheduler().getCurrentTimeMs();
  }

  @Override
  public Scheduler getInternalSchedulerForTest() {
    return resources.getInternalScheduler();
  }

  @Override
  public Storage getStorage() {
    return storage;
  }

  @Override
  public NetworkEndpointId getNetworkIdForTest() {
    NetworkChannel network = resources.getNetwork();
    if (!(network instanceof TestableNetworkChannel)) {
      throw new UnsupportedOperationException(
          "getNetworkIdForTest requires a TestableNetworkChannel, not: " + network.getClass());
    }
    return ((TestableNetworkChannel) network).getNetworkIdForTest();
  }

  // End of methods for TestableInvalidationClient

  @Override
  public void start() {
    Preconditions.checkState(resources.isStarted(), "Resources must be started before starting " +
        "the Ticl");
    Preconditions.checkState(!ticlState.isStarted(), "Already started");

    // Initialize the nonce so that we can maintain the invariant that exactly one of
    // "nonce" and "clientToken" is non-null.
    setNonce(ByteString.copyFromUtf8(Long.toString(internalScheduler.getCurrentTimeMs())));

    logger.info("Starting with Java config: %s", config);
    // Read the state blob and then schedule startInternal once the value is there.
    scheduleStartAfterReadingStateBlob();
  }

  /**
   * Implementation of {@link #start} on the internal thread with the persistent
   * {@code serializedState} if any. Starts the TICL protocol and makes the TICL ready to receive
   * registrations, invalidations, etc.
   */
  private void startInternal(byte[] serializedState) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // Initialize the session manager using the persisted client token.
    PersistentTiclState persistentState =
        (serializedState == null) ? null : PersistenceUtils.deserializeState(logger,
            serializedState, digestFn);

    if ((serializedState != null) && (persistentState == null)) {
      // In this case, we'll proceed as if we had no persistent state -- i.e., obtain a new client
      // id from the server.
      statistics.recordError(ClientErrorType.PERSISTENT_DESERIALIZATION_FAILURE);
      logger.severe("Failed deserializing persistent state: %s",
          CommonProtoStrings2.toLazyCompactString(serializedState));
    }
    if (persistentState != null) {
      // If we have persistent state, use the previously-stored token and send a heartbeat to
      // let the server know that we've restarted, since we may have been marked offline.

      // In the common case, the server will already have all of our
      // registrations, but we won't know for sure until we've gotten its summary.
      // We'll ask the application for all of its registrations, but to avoid
      // making the registrar redo the work of performing registrations that
      // probably already exist, we'll suppress sending them to the registrar.
      logger.info("Restarting from persistent state: %s",
          CommonProtoStrings2.toLazyCompactString(persistentState.getClientToken()));
      setNonce(null);
      setClientToken(persistentState.getClientToken());
      shouldSendRegistrations = false;

      // Schedule an info message for the near future.
      int initialHeartbeatDelayMs = computeInitialPersistentHeartbeatDelayMs(
          config, resources, persistentState.getLastMessageSendTimeMs());
      internalScheduler.schedule(initialHeartbeatDelayMs,
          new NamedRunnable("InvClient.sendInfoMessageAfterPersistentRead") {
        @Override
        public void run() {
          sendInfoMessageToServer(false, true);
        }
      });

      // We need to ensure that heartbeats are sent, regardless of whether we start fresh or from
      // persistent state. The line below ensures that they are scheduled in the persistent startup
      // case. For the other case, the task is scheduled when we acquire a token.
      heartbeatTask.ensureScheduled("Startup-after-persistence");
    } else {
      // If we had no persistent state or couldn't deserialize the state that we had, start fresh.
      // Request a new client identifier.

      // The server can't possibly have our registrations, so whatever we get
      // from the application we should send to the registrar.
      logger.info("Starting with no previous state");
      shouldSendRegistrations = true;
      acquireToken("Startup");
    }

    // listener.ready() is called when ticl has acquired a new token.
  }

  /**
   * Returns the delay for the initial heartbeat, given that the last message to the server was
   * sent at {@code lastSendTimeMs}.
   * @param config configuration object used by the client
   * @param resources resources used by the client
   */
  
  static int computeInitialPersistentHeartbeatDelayMs(ClientConfigP config,
      SystemResources resources, long lastSendTimeMs) {
      // There are five cases:
      // 1. Channel does not support offline delivery. We delay a little bit to allow the
      // application to reissue its registrations locally and avoid triggering registration
      // sync with the data center due to a hash mismatch. This is the "default delay," and we
      // never use a delay less than it.
      //
      // All other cases are for channels supporting offline delivery.
      //
      // 2. Last send time is in the future (something weird happened). Use the default delay.
      // 3. We have been asleep for more than one heartbeat interval. Use the default delay.
      // 4. We have been asleep for less than one heartbeat interval.
      //    (a). The time remaining to the end of the interval is less than the default delay.
      //         Use the default delay.
      //    (b). The time remaining to the end of the interval is more than the default delay.
      //         Use the remaining delay.
    final long nowMs = resources.getInternalScheduler().getCurrentTimeMs();
    final int initialHeartbeatDelayMs;
    if (!config.getChannelSupportsOfflineDelivery()) {
      // Case 1.
      initialHeartbeatDelayMs = config.getInitialPersistentHeartbeatDelayMs();
    } else {
      // Offline delivery cases (2, 3, 4).
      // The default of the last send time is zero, so even if it wasn't written in the persistent
      // state, this logic is still correct.
      if ((lastSendTimeMs > nowMs) ||                                       // Case 2.
          ((lastSendTimeMs + config.getHeartbeatIntervalMs()) < nowMs)) {   // Case 3.
        // Either something strange happened and the last send time is in the future, or we
        // have been asleep for more than one heartbeat interval. Send immediately.
        initialHeartbeatDelayMs = config.getInitialPersistentHeartbeatDelayMs();
      } else {
        // Case 4.
        // We have been asleep for less than one heartbeat interval. Send after it expires,
        // but ensure we let the initial heartbeat interval elapse.
        final long timeSinceLastMessageMs = nowMs - lastSendTimeMs;
        final int remainingHeartbeatIntervalMs =
             (int) (config.getHeartbeatIntervalMs() - timeSinceLastMessageMs);
        initialHeartbeatDelayMs = Math.max(remainingHeartbeatIntervalMs,
            config.getInitialPersistentHeartbeatDelayMs());
      }
    }
    resources.getLogger().info("Computed heartbeat delay %s from: offline-delivery = %s, "
        + "initial-persistent-delay = %s, heartbeat-interval = %s, nowMs = %s",
        initialHeartbeatDelayMs, config.getChannelSupportsOfflineDelivery(),
        config.getInitialPersistentHeartbeatDelayMs(), config.getHeartbeatIntervalMs(),
        nowMs);
    return initialHeartbeatDelayMs;
  }

  @Override
  public void stop() {
    logger.info("Ticl being stopped: %s", InvalidationClientImpl.this);
    if (ticlState.isStarted()) {  // RunState is thread-safe.
      ticlState.stop();
    }
  }

  @Override
  public void register(ObjectId objectId) {
    List<ObjectId> objectIds = new ArrayList<ObjectId>();
    objectIds.add(objectId);
    performRegisterOperations(objectIds, RegistrationP.OpType.REGISTER);
  }

  @Override
  public void unregister(ObjectId objectId) {
    List<ObjectId> objectIds = new ArrayList<ObjectId>();
    objectIds.add(objectId);
    performRegisterOperations(objectIds, RegistrationP.OpType.UNREGISTER);
  }

  @Override
  public void register(Collection<ObjectId> objectIds) {
    performRegisterOperations(objectIds, RegistrationP.OpType.REGISTER);
  }

  @Override
  public void unregister(Collection<ObjectId> objectIds) {
    performRegisterOperations(objectIds, RegistrationP.OpType.UNREGISTER);
  }

  /**
   * Implementation of (un)registration.
   *
   * @param objectIds object ids on which to operate
   * @param regOpType whether to register or unregister
   */
  private void performRegisterOperations(final Collection<ObjectId> objectIds,
      final RegistrationP.OpType regOpType) {
    Preconditions.checkState(!objectIds.isEmpty(), "Must specify some object id");
    Preconditions.checkNotNull(regOpType, "Must specify (un)registration");

    Preconditions.checkState(ticlState.isStarted() || ticlState.isStopped(),
      "Cannot call %s for object %s when the Ticl has not been started. If start has been " +
      "called, caller must wait for InvalidationListener.ready", regOpType, objectIds);
    if (ticlState.isStopped()) {
      // The Ticl has been stopped. This might be some old registration op coming in. Just ignore
      // instead of crashing.
      logger.warning("Ticl stopped: register (%s) of %s ignored.", regOpType, objectIds);
      return;
    }

    internalScheduler.schedule(NO_DELAY, new NamedRunnable("InvClient.performRegOperations") {
      @Override
      public void run() {
        List<ObjectIdP> objectIdProtos = new ArrayList<ObjectIdP>(objectIds.size());
        for (ObjectId objectId : objectIds) {
          Preconditions.checkNotNull(objectId, "Must specify object id");
          ObjectIdP objectIdProto = ProtoConverter.convertToObjectIdProto(objectId);
          IncomingOperationType opType = (regOpType == RegistrationP.OpType.REGISTER) ?
              IncomingOperationType.REGISTRATION : IncomingOperationType.UNREGISTRATION;
          statistics.recordIncomingOperation(opType);
          logger.info("Register %s, %s", CommonProtoStrings2.toLazyCompactString(objectIdProto),
              regOpType);
          objectIdProtos.add(objectIdProto);
          // Inform immediately of success so that the application is informed even if the reply
          // message from the server is lost. When we get a real ack from the server, we do
          // not need to inform the application.
          InvalidationListener.RegistrationState regState = convertOpTypeToRegState(regOpType);
          listener.informRegistrationStatus(InvalidationClientImpl.this, objectId, regState);
        }

        // Update the registration manager state, then have the protocol client send a message.
        // performOperations returns only those elements of objectIdProtos that caused a state
        // change (i.e., elements not present if regOpType == REGISTER or elements that were present
        // if regOpType == UNREGISTER).
        Collection<ObjectIdP> objectProtosToSend = registrationManager.performOperations(
            objectIdProtos, regOpType);

        // Check whether we should suppress sending registrations because we don't
        // yet know the server's summary.
        if (shouldSendRegistrations && (!objectProtosToSend.isEmpty())) {
          protocolHandler.sendRegistrations(objectProtosToSend, regOpType);
        }
        InvalidationClientImpl.this.regSyncHeartbeatTask.ensureScheduled("performRegister");
      }
    });
  }

  @Override
  public void acknowledge(final AckHandle acknowledgeHandle) {
    Preconditions.checkNotNull(acknowledgeHandle);
    internalScheduler.schedule(NO_DELAY, new NamedRunnable("InvClient.acknowledge") {
      @Override
      public void run() {
        // Validate the ack handle.

        // 1. Parse the ack handle first.
        AckHandleP ackHandle;
        try {
          ackHandle = AckHandleP.parseFrom(acknowledgeHandle.getHandleData());
        } catch (InvalidProtocolBufferException exception) {
          logger.warning("Bad ack handle : %s",
            CommonProtoStrings2.toLazyCompactString(acknowledgeHandle.getHandleData()));
          statistics.recordError(ClientErrorType.ACKNOWLEDGE_HANDLE_FAILURE);
          return;
        }

        // 2. Validate ack handle - it should have a valid invalidation.
        if (!ackHandle.hasInvalidation() ||
            !msgValidator.isValid(ackHandle.getInvalidation())) {
          logger.warning("Incorrect ack handle data: %s", acknowledgeHandle);
          statistics.recordError(ClientErrorType.ACKNOWLEDGE_HANDLE_FAILURE);
          return;
        }

        // Currently, only invalidations have non-trivial ack handle.
        InvalidationP invalidation = ackHandle.getInvalidation();
        if (invalidation.hasPayload()) {
          // Don't send the payload back.
          invalidation = invalidation.toBuilder().clearPayload().build();
        }
        statistics.recordIncomingOperation(IncomingOperationType.ACKNOWLEDGE);
        protocolHandler.sendInvalidationAck(invalidation);
      }
    });
  }

  //
  // Protocol listener methods
  //

  @Override
  public ByteString getClientToken() {
    Preconditions.checkState((clientToken == null) || (nonce == null));
    return clientToken;
  }

  @Override
  public void handleTokenChanged(final ServerMessageHeader header,
      final ByteString newToken) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // If the client token was valid, we have already checked in protocol handler.
    // Otherwise, we need to check for the nonce, i.e., if we have a nonce, the message must
    // carry the same nonce.
    if (nonce != null) {
      if (TypedUtil.<ByteString>equals(header.token, nonce)) {
        logger.info("Accepting server message with matching nonce: %s",
          CommonProtoStrings2.toLazyCompactString(nonce));
        setNonce(null);
      } else {
        statistics.recordError(ClientErrorType.NONCE_MISMATCH);
        logger.info("Rejecting server message with mismatched nonce: Client = %s, Server = %s",
            CommonProtoStrings2.toLazyCompactString(nonce),
            CommonProtoStrings2.toLazyCompactString(header.token));
        return;
      }
    }

    // The message is for us. Process it.
    handleIncomingHeader(header);

    if (newToken == null) {
      logger.info("Destroying existing token: %s",
        CommonProtoStrings2.toLazyCompactString(clientToken));
      acquireToken("Destroy");
    } else {
      logger.info("New token being assigned at client: %s, Old = %s",
        CommonProtoStrings2.toLazyCompactString(newToken),
        CommonProtoStrings2.toLazyCompactString(clientToken));

      // We just received a new token. Start the regular heartbeats now.
      heartbeatTask.ensureScheduled("Heartbeat-after-new-token");
      setNonce(null);
      setClientToken(newToken);
      persistentWriteTask.ensureScheduled("Write-after-new-token");
    }
  }

  @Override
  public void handleIncomingHeader(ServerMessageHeader header) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    Preconditions.checkState(nonce == null,
        "Cannot process server header with non-null nonce (have %s): %s", nonce, header);
    if (header.registrationSummary != null) {
      // We've received a summary from the server, so if we were suppressing
      // registrations, we should now allow them to go to the registrar.
      shouldSendRegistrations = true;
      registrationManager.informServerRegistrationSummary(header.registrationSummary);
    }
  }

  @Override
  public void handleInvalidations(final ServerMessageHeader header,
      final Collection<InvalidationP> invalidations) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    handleIncomingHeader(header);

    for (InvalidationP invalidation : invalidations) {
      AckHandle ackHandle = AckHandle.newInstance(
          CommonProtos2.newAckHandleP(invalidation).toByteArray());
      if (CommonProtos2.isAllObjectId(invalidation.getObjectId())) {
        logger.info("Issuing invalidate all");
        listener.invalidateAll(InvalidationClientImpl.this, ackHandle);
      } else {
        // Regular object. Could be unknown version or not.
        Invalidation inv = ProtoConverter.convertFromInvalidationProto(invalidation);
        logger.info("Issuing invalidate (known-version = %s): %s", invalidation.getIsKnownVersion(),
            inv);
        if (invalidation.getIsKnownVersion()) {
          listener.invalidate(InvalidationClientImpl.this, inv, ackHandle);
        } else {
          // Unknown version
          listener.invalidateUnknownVersion(InvalidationClientImpl.this, inv.getObjectId(),
              ackHandle);
        }
      }
    }
  }

  @Override
  public void handleRegistrationStatus(final ServerMessageHeader header,
      final List<RegistrationStatus> regStatusList) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    handleIncomingHeader(header);

    List<Boolean> localProcessingStatuses =
        registrationManager.handleRegistrationStatus(regStatusList);
    Preconditions.checkState(localProcessingStatuses.size() == regStatusList.size(),
        "Not all registration statuses were processed");

    // Inform app about the success or failure of each registration based
    // on what the registration manager has indicated.
    for (int i = 0; i < regStatusList.size(); ++i) {
      RegistrationStatus regStatus = regStatusList.get(i);
      boolean wasSuccess = localProcessingStatuses.get(i);
      logger.fine("Process reg status: %s", regStatus);

      // Only inform in the case of failure since the success path has already
      // been dealt with (the ticl issued informRegistrationStatus immediately
      // after receiving the register/unregister call).
      ObjectId objectId = ProtoConverter.convertFromObjectIdProto(
        regStatus.getRegistration().getObjectId());
      if (!wasSuccess) {
        String description = CommonProtos2.isSuccess(regStatus.getStatus()) ?
            "Registration discrepancy detected" : regStatus.getStatus().getDescription();

        // Note "success" shows up as transient failure in this scenario.
        boolean isPermanent = CommonProtos2.isPermanentFailure(regStatus.getStatus());
        listener.informRegistrationFailure(InvalidationClientImpl.this, objectId, !isPermanent,
            description);
      }
    }
  }

  @Override
  public void handleRegistrationSyncRequest(final ServerMessageHeader header) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    // Send all the registrations in the reg sync message.
    handleIncomingHeader(header);

    // Generate a single subtree for all the registrations.
    RegistrationSubtree subtree =
        registrationManager.getRegistrations(Bytes.EMPTY_BYTES.getByteArray(), 0);
    protocolHandler.sendRegistrationSyncSubtree(subtree);
  }

  @Override
  public void handleInfoMessage(ServerMessageHeader header, Collection<InfoType> infoTypes) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    handleIncomingHeader(header);
    boolean mustSendPerformanceCounters = false;
    for (InfoType infoType : infoTypes) {
      mustSendPerformanceCounters = (infoType == InfoType.GET_PERFORMANCE_COUNTERS);
      if (mustSendPerformanceCounters) {
        break;
      }
    }
    sendInfoMessageToServer(mustSendPerformanceCounters,
        !registrationManager.isStateInSyncWithServer());
  }

  @Override
  public void handleErrorMessage(ServerMessageHeader header, ErrorMessage.Code code,
      String description) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    handleIncomingHeader(header);

    // If it is an auth failure, we shut down the ticl.
    logger.severe("Received error message: %s, %s, %s", header, code, description);

    // Translate the code to error reason.
    int reason;
    switch (code) {
      case AUTH_FAILURE:
        reason = ErrorInfo.ErrorReason.AUTH_FAILURE;
        break;
      case UNKNOWN_FAILURE:
        reason = ErrorInfo.ErrorReason.UNKNOWN_FAILURE;
        break;
      default:
        reason = ErrorInfo.ErrorReason.UNKNOWN_FAILURE;
        break;
    }

    // Issue an informError to the application.
    ErrorInfo errorInfo = ErrorInfo.newInstance(reason, false, description, null);
    listener.informError(this, errorInfo);

    // If this is an auth failure, remove registrations and stop the Ticl. Otherwise do nothing.
    if (code != ErrorMessage.Code.AUTH_FAILURE) {
      return;
    }

    // If there are any registrations, remove them and issue registration failure.
    Collection<ObjectIdP> desiredRegistrations = registrationManager.removeRegisteredObjects();
    logger.warning("Issuing failure for %s objects", desiredRegistrations.size());
    for (ObjectIdP objectId : desiredRegistrations) {
      listener.informRegistrationFailure(this,
        ProtoConverter.convertFromObjectIdProto(objectId), false, "Auth error: " + description);
    }
    // Schedule the stop on the listener work queue so that it happens after the inform
    // registration failure calls above
    resources.getListenerScheduler().schedule(NO_DELAY,
        new NamedRunnable("InvClient.scheduleStopAfterAuthError") {
      @Override
      public void run() {
        stop();
      }
    });
  }

  @Override
  public void handleMessageSent() {
    // The ProtocolHandler just sent a message to the server. If the channel supports offline
    // delivery (see the comment in the ClientConfigP), store this time to stable storage. This
    // only needs to be a best-effort write; if it fails, then we will "forget" that we sent the
    // message and heartbeat needlessly when next restarted. That is a performance/battery bug,
    // not a correctness bug.
    lastMessageSendTimeMs = getResourcesTimeMs();
    if (config.getChannelSupportsOfflineDelivery()) {
      // Write whether or not we have a token. The persistent write task is a no-op if there is
      // no token. We only write if the channel supports offline delivery. We could do the write
      // regardless, and may want to do so in the future, since it might simplify some of the
      // Ticl implementation.
      persistentWriteTask.ensureScheduled("sent-message");
    }
  }

  @Override
  public RegistrationSummary getRegistrationSummary() {
    return registrationManager.getRegistrationSummary();
  }

  //
  // Private methods and toString.
  //

  /**
   * Requests a new client identifier from the server.
   * <p>
   * REQUIRES: no token currently be held.
   *
   * @param debugString information to identify the caller
   */
  private void acquireToken(final String debugString) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // Clear the current token and schedule the token acquisition.
    setClientToken(null);
    acquireTokenTask.ensureScheduled(debugString);
  }

  /**
   * Sends an info message to the server. If {@code mustSendPerformanceCounters} is true,
   * the performance counters are sent regardless of when they were sent earlier.
   */
  private void sendInfoMessageToServer(boolean mustSendPerformanceCounters,
      boolean requestServerSummary) {
    logger.info("Sending info message to server");
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    List<SimplePair<String, Integer>> performanceCounters =
        new ArrayList<SimplePair<String, Integer>>();
    List<SimplePair<String, Integer>> configParams =
        new ArrayList<SimplePair<String, Integer>>();
    ClientConfigP configToSend = null;
    if (mustSendPerformanceCounters) {
      statistics.getNonZeroStatistics(performanceCounters);
      configToSend = config;
    }
    protocolHandler.sendInfoMessage(performanceCounters, configToSend, requestServerSummary);
  }

  /** Reads the Ticl state from persistent storage (if any) and calls {@code startInternal}. */
  private void scheduleStartAfterReadingStateBlob() {
    storage.readKey(CLIENT_TOKEN_KEY, new Callback<SimplePair<Status, byte[]>>() {
      @Override
      public void accept(final SimplePair<Status, byte[]> readResult) {
        final byte[] serializedState = readResult.getFirst().isSuccess() ?
            readResult.getSecond() : null;
        // Call start now.
        if (!readResult.getFirst().isSuccess()) {
          statistics.recordError(ClientErrorType.PERSISTENT_READ_FAILURE);
          logger.warning("Could not read state blob: %s", readResult.getFirst().getMessage());
        }
        startInternal(serializedState);
      }
    });
  }

  /**
   * Converts an operation type {@code regOpType} to a
   * {@code InvalidationListener.RegistrationState}.
   */
  private static InvalidationListener.RegistrationState convertOpTypeToRegState(
      RegistrationP.OpType regOpType) {
    InvalidationListener.RegistrationState regState =
        regOpType == RegistrationP.OpType.REGISTER ?
            InvalidationListener.RegistrationState.REGISTERED :
              InvalidationListener.RegistrationState.UNREGISTERED;
    return regState;
  }

  /**
   * Sets the nonce to {@code newNonce}.
   * <p>
   * REQUIRES: {@code newNonce} be null or {@link #clientToken} be null.
   * The goal is to ensure that a nonce is never set unless there is no
   * client token, unless the nonce is being cleared.
   */
  private void setNonce(ByteString newNonce) {
    Preconditions.checkState((newNonce == null) || (clientToken == null),
        "Tried to set nonce with existing token %s", clientToken);
    this.nonce = newNonce;
  }

  /**
   * Sets the clientToken to {@code newClientToken}.
   * <p>
   * REQUIRES: {@code newClientToken} be null or {@link #nonce} be null.
   * The goal is to ensure that a token is never set unless there is no
   * nonce, unless the token is being cleared.
   */
  private void setClientToken(ByteString newClientToken) {
    Preconditions.checkState((newClientToken == null) || (nonce == null),
        "Tried to set token with existing nonce %s", nonce);

    // If the ticl is in the process of being started and we are getting a new token (either from
    // persistence or from the server, start the ticl and inform the application.
    boolean finishStartingTicl = !ticlState.isStarted() &&
       (clientToken == null) && (newClientToken != null);
    this.clientToken = newClientToken;

    if (finishStartingTicl) {
      finishStartingTiclAndInformListener();
    }
  }

  /** Start the ticl and inform the listener that it is ready. */
  private void finishStartingTiclAndInformListener() {
    Preconditions.checkState(!ticlState.isStarted());
    ticlState.start();
    listener.ready(this);

    // We are not currently persisting our registration digest, so regardless of whether or not
    // we are restarting from persistent state, we need to query the application for all of
    // its registrations.
    listener.reissueRegistrations(InvalidationClientImpl.this, RegistrationManager.EMPTY_PREFIX, 0);
    logger.info("Ticl started: %s", this);
  }

  /**
   * Returns an exponential backoff generator with a max exponential factor given by
   * {@code config.getMaxExponentialBackoffFactor()} and initial delay {@code initialDelayMs}.
   */
  private ExponentialBackoffDelayGenerator createExpBackOffGenerator(int initialDelayMs) {
    return new ExponentialBackoffDelayGenerator(random, initialDelayMs,
        config.getMaxExponentialBackoffFactor());
  }

  @Override
  public void toCompactString(TextBuilder builder) {
    builder.appendFormat("Client: %s, %s", applicationClientId,
        CommonProtoStrings2.toLazyCompactString(clientToken));
  }
}
