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
import com.google.ipc.invalidation.common.TiclMessageValidator2;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.SystemResources.NetworkChannel;
import com.google.ipc.invalidation.external.client.SystemResources.Scheduler;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.ipc.invalidation.external.client.types.SimplePair;
import com.google.ipc.invalidation.ticl.Statistics.ClientErrorType;
import com.google.ipc.invalidation.ticl.Statistics.ReceivedMessageType;
import com.google.ipc.invalidation.ticl.Statistics.SentMessageType;
import com.google.ipc.invalidation.util.InternalBase;
import com.google.ipc.invalidation.util.NamedRunnable;
import com.google.ipc.invalidation.util.Smearer;
import com.google.ipc.invalidation.util.TextBuilder;
import com.google.ipc.invalidation.util.TypedUtil;
import com.google.protobuf.ByteString;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protos.ipc.invalidation.ClientProtocol.ApplicationClientIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientConfigP;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientHeader;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientToServerMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientVersion;
import com.google.protos.ipc.invalidation.ClientProtocol.ConfigChangeMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.ErrorMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.InfoMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.InfoRequestMessage.InfoType;
import com.google.protos.ipc.invalidation.ClientProtocol.InitializeMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.InitializeMessage.DigestSerializationType;
import com.google.protos.ipc.invalidation.ClientProtocol.InvalidationMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.InvalidationP;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.PropertyRecord;
import com.google.protos.ipc.invalidation.ClientProtocol.ProtocolHandlerConfigP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationStatus;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSubtree;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSummary;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSyncMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.ServerHeader;
import com.google.protos.ipc.invalidation.ClientProtocol.ServerToClientMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.TokenControlMessage;

import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;


/**
 * A layer for interacting with low-level protocol messages.  Parses messages from the server and
 * calls appropriate functions on the {@code ProtocolListener} to handle various types of message
 * content.  Also buffers message data from the client and constructs and sends messages to the
 * server.
 *
 */
class ProtocolHandler {

  /**
   * The task that is scheduled to send batched messages to the server (when
   * needed).
   **/
  private class BatchingTask extends RecurringTask {
    BatchingTask(Smearer smearer, int batchingDelayMs) {
      super("Batching", internalScheduler, logger, smearer, null,
          batchingDelayMs, NO_DELAY);
    }

    @Override
    public boolean runTask() {
      // Send message to server - the batching information is picked up in sendMessageToServer.
      // Go through a throttler to ensure that we obey rate limits in sending messages.
      throttle.fire();
      return false;  // Don't reschedule.
    }
  }

  /** Representation of a message header for use in a server message. */
  static class ServerMessageHeader extends InternalBase {
    /**
     * Constructs an instance.
     *
     * @param token server-sent token
     * @param registrationSummary summary over server registration state
     */
    ServerMessageHeader(ByteString token, RegistrationSummary registrationSummary) {
      this.token = token;
      this.registrationSummary = registrationSummary;
    }

    /** Server-sent token. */
    ByteString token;

    /** Summary of the client's registration state at the server. */
    RegistrationSummary registrationSummary;

    @Override
    public void toCompactString(TextBuilder builder) {
      builder.appendFormat("Token: %s, Summary: %s", CommonProtoStrings2.toLazyCompactString(token),
          registrationSummary);
    }
  }

  /**
   * Listener for protocol events. The protocol client calls these methods when a message is
   * received from the server. It guarantees that the call will be made on the internal thread that
   * the SystemResources provides. When the protocol listener is called, the token has been checked
   * and message validation has been completed (using the {@link TiclMessageValidator2}).
   * That is, all of the methods below can assume that the nonce is null and the server token is
   * valid.
   */
  interface ProtocolListener {

    /**
     * Handles an incoming message from the server. This method may be called in addition
     * to the handle* methods below - so the listener code should be prepared for it.
     *
     * @param header server message header
     */
    void handleIncomingHeader(ServerMessageHeader header);

    /**
     * Handles a token change event from the server
     *
     * @param header server message header
     * @param newToken a new token for the client. If {@code null}, it means destroy the token.
     */
    void handleTokenChanged(ServerMessageHeader header, ByteString newToken);

    /**
     * Handles {@code invalidations} from the server
     * @param header server message header
     */
    void handleInvalidations(ServerMessageHeader header, Collection<InvalidationP> invalidations);

    /**
     * Handles registration updates from the server
     * @param header server message header
     * @param regStatus registration updates
     */
    void handleRegistrationStatus(ServerMessageHeader header, List<RegistrationStatus> regStatus);

    /**
     * Handles a registration sync request from the server
     * @param header server message header
     */
    void handleRegistrationSyncRequest(ServerMessageHeader header);

    /**
     * Handles an info message from the server
     * @param header server message header
     * @param infoTypes types of info requested
     */
    void handleInfoMessage(ServerMessageHeader header, Collection<InfoType> infoTypes);

    /**
     * Handles an error message from the server
     * @param code error reason
     * @param description human-readable description of the error
     */
    void handleErrorMessage(ServerMessageHeader header, ErrorMessage.Code code, String description);

    /** Records that a message was sent to the server at the current time. */
    void handleMessageSent();

    /** Returns a summary of the current desired registrations. */
    RegistrationSummary getRegistrationSummary();

    /** Returns the current server-assigned client token, if any. */
    ByteString getClientToken();
  }

  /** Information about the client, e.g., application name, OS, etc. */
  private final ClientVersion clientVersion;

  /** A logger. */
  private final Logger logger;

  /** Scheduler for the client's internal processing. */
  private final Scheduler internalScheduler;

  /** Network channel for sending and receiving messages to and from the server. */
  private final NetworkChannel network;

  /** A throttler to prevent the client from sending too many messages in a given interval. */
  private final Throttle throttle;

  /** The protocol listener. */
  private final ProtocolListener listener;

  /** Checks that messages (inbound and outbound) conform to basic validity constraints. */
  private final TiclMessageValidator2 msgValidator;

  /** A debug message id that is added to every message to the server. */
  private int messageId = 1;

  // State specific to a client. If we want to support multiple clients, this could
  // be in a map or could be eliminated (e.g., no batching).

  /** The last known time from the server. */
  private long lastKnownServerTimeMs = 0;

  /**
   * The next time before which a message cannot be sent to the server. If this is less than current
   * time, a message can be sent at any time.
   */
  private long nextMessageSendTimeMs = 0;

  /** Set of pending registrations stored as a map for overriding later operations. */
  private final Map<ProtoWrapper<ObjectIdP>, RegistrationP.OpType> pendingRegistrations =
      new HashMap<ProtoWrapper<ObjectIdP>, RegistrationP.OpType>();

  /** Set of pending invalidation acks. */
  private final Set<ProtoWrapper<InvalidationP>> pendingAckedInvalidations =
      new HashSet<ProtoWrapper<InvalidationP>>();

  /** Set of pending registration sub trees for registration sync. */
  private final Set<ProtoWrapper<RegistrationSubtree>> pendingRegSubtrees =
      new HashSet<ProtoWrapper<RegistrationSubtree>>();

  /** Pending initialization message to send to the server, if any. */
  private InitializeMessage pendingInitializeMessage = null;

  /** Pending info message to send to the server, if any. */
  private InfoMessage pendingInfoMessage = null;

  /** Statistics objects to track number of sent messages, etc. */
  private final Statistics statistics;

  /** Task to send all batched messages to the server. */
  private final RecurringTask batchingTask;
  /**
   * Creates an instance.
   *
   * @param config configuration for the client
   * @param resources resources to use
   * @param smearer a smearer to randomize delays
   * @param statistics track information about messages sent/received, etc
   * @param applicationName name of the application using the library (for debugging/monitoring)
   * @param listener callback for protocol events
   */
  ProtocolHandler(ProtocolHandlerConfigP config, final SystemResources resources,
      Smearer smearer, Statistics statistics, String applicationName, ProtocolListener listener,
      TiclMessageValidator2 msgValidator) {
    this.logger = resources.getLogger();
    this.statistics = statistics;
    this.internalScheduler = resources.getInternalScheduler();
    this.network = resources.getNetwork();
    this.listener = listener;
    this.msgValidator = msgValidator;
    this.throttle = new Throttle(config.getRateLimitList(), internalScheduler,
        new NamedRunnable("ProtocolHandler.throttle") {
      @Override
      public void run() {
        sendMessageToServer();
      }
    });

    this.batchingTask = new BatchingTask(smearer, config.getBatchingDelayMs());

    this.clientVersion = CommonProtos2.newClientVersion(resources.getPlatform(), "Java",
        applicationName);

    // Install ourselves as a receiver for server messages.
    network.setMessageReceiver(new Callback<byte[]>() {
      @Override
      public void accept(final byte[] incomingMessage) {
        internalScheduler.schedule(NO_DELAY, new NamedRunnable("ProtocolHandler.handleMessage") {
          @Override
          public void run() {
            handleIncomingMessage(incomingMessage);
          }
        });
      }
    });
    network.addNetworkStatusReceiver(new Callback<Boolean>() {
      @Override
      public void accept(Boolean isOnline) {
        // Do nothing for now.
      }
    });
    logger.info("Created protocol handler for application %s, platform %s", applicationName,
        resources.getPlatform());
  }

  /** Returns a default config for the protocol handler. */
  static ProtocolHandlerConfigP.Builder createConfig() {
    // Allow at most 1 message every 1000 msec.
    int window0Ms = 1000;
    int numMessagesPerWindow0 = 1;

    // Allow at most 6 messages every minute.
    int window1Ms = 60 * 1000;
    int numMessagesPerWindow1 = 6;

    return ProtocolHandlerConfigP.newBuilder()
        .addRateLimit(CommonProtos2.newRateLimitP(window0Ms, numMessagesPerWindow0))
        .addRateLimit(CommonProtos2.newRateLimitP(window1Ms, numMessagesPerWindow1));
  }

  /** Returns a configuration object with parameters set for unit tests. */
  static ProtocolHandlerConfigP.Builder createConfigForTest() {
    // No rate limits
    int smallBatchDelayForTest = 200;
    return ProtocolHandlerConfigP.newBuilder().setBatchingDelayMs(smallBatchDelayForTest);
  }

  /**
   * Returns the next time a message is allowed to be sent to the server.  Typically, this will be
   * in the past, meaning that the client is free to send a message at any time.
   */
  public long getNextMessageSendTimeMsForTest() {
    return nextMessageSendTimeMs;
  }

  /** Handles a message from the server. */
  private void handleIncomingMessage(byte[] incomingMessage) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    ServerToClientMessage message;
    try {
      message = ServerToClientMessage.parseFrom(incomingMessage);
    } catch (InvalidProtocolBufferException exception) {
      logger.warning("Incoming message is unparseable: %s",
          CommonProtoStrings2.toLazyCompactString(incomingMessage));
      return;
    }

    // Validate the message. If this passes, we can blindly assume valid messages from here on.
    logger.fine("Incoming message: %s", message);
    if (!msgValidator.isValid(message)) {
      statistics.recordError(ClientErrorType.INCOMING_MESSAGE_FAILURE);
      logger.severe("Received invalid message: %s", message);
      return;
    }

    statistics.recordReceivedMessage(ReceivedMessageType.TOTAL);

    // Construct a representation of the message header.
    ServerHeader messageHeader = message.getHeader();
    ServerMessageHeader header = new ServerMessageHeader(messageHeader.getClientToken(),
        messageHeader.hasRegistrationSummary() ? messageHeader.getRegistrationSummary() : null);

    // Check the version of the message.
    if (messageHeader.getProtocolVersion().getVersion().getMajorVersion() !=
        CommonInvalidationConstants2.PROTOCOL_MAJOR_VERSION) {
      statistics.recordError(ClientErrorType.PROTOCOL_VERSION_FAILURE);
      logger.severe("Dropping message with incompatible version: %s", message);
      return;
    }

    // Check if it is a ConfigChangeMessage which indicates that messages should no longer be
    // sent for a certain duration. Perform this check before the token is even checked.
    if (message.hasConfigChangeMessage()) {
      ConfigChangeMessage configChangeMsg = message.getConfigChangeMessage();
      statistics.recordReceivedMessage(ReceivedMessageType.CONFIG_CHANGE);
      if (configChangeMsg.hasNextMessageDelayMs()) {  // Validator has ensured that it is positive.
        nextMessageSendTimeMs =
            internalScheduler.getCurrentTimeMs() + configChangeMsg.getNextMessageDelayMs();
      }
      return;  // Ignore all other messages in the envelope.
    }

    // Check token if possible.
    if (!checkServerToken(messageHeader.getClientToken())) {
      return;
    }

    lastKnownServerTimeMs = Math.max(lastKnownServerTimeMs, messageHeader.getServerTimeMs());

    // Invoke callbacks as appropriate.
    if (message.hasTokenControlMessage()) {
      TokenControlMessage tokenMsg = message.getTokenControlMessage();
      statistics.recordReceivedMessage(ReceivedMessageType.TOKEN_CONTROL);
      listener.handleTokenChanged(header, tokenMsg.hasNewToken() ? tokenMsg.getNewToken() : null);
    }

    // We explicitly check to see if we have a valid token after we pass the token control message
    // to the listener. This is because we can't determine whether we have a valid token until
    // after the upcall:
    // 1) The listener might have acquired a token.
    // 2) The listener might have lost its token.
    // Note that checking for the presence of a TokenControlMessage is *not* sufficient: it might
    // be a token-assign with the wrong nonce or a token-destroy message, for example.
    if (listener.getClientToken() == null) {
      logger.warning("Ignoring incoming message because no client token: %s", message);
      return;
    }

    // Handle the messages received from the server by calling the appropriate listener method.

    // In the beginning inform the listener about the header (the caller is already prepared
    // to handle the fact that the same header is given to it multiple times).
    listener.handleIncomingHeader(header);

    if (message.hasInvalidationMessage()) {
      statistics.recordReceivedMessage(ReceivedMessageType.INVALIDATION);
      listener.handleInvalidations(header, message.getInvalidationMessage().getInvalidationList());
    }
    if (message.hasRegistrationStatusMessage()) {
      statistics.recordReceivedMessage(ReceivedMessageType.REGISTRATION_STATUS);
      listener.handleRegistrationStatus(header,
          message.getRegistrationStatusMessage().getRegistrationStatusList());
    }
    if (message.hasRegistrationSyncRequestMessage()) {
      statistics.recordReceivedMessage(ReceivedMessageType.REGISTRATION_SYNC_REQUEST);
      listener.handleRegistrationSyncRequest(header);
    }
    if (message.hasInfoRequestMessage()) {
      statistics.recordReceivedMessage(ReceivedMessageType.INFO_REQUEST);
      listener.handleInfoMessage(header, message.getInfoRequestMessage().getInfoTypeList());
    }
    if (message.hasErrorMessage()) {
      statistics.recordReceivedMessage(ReceivedMessageType.ERROR);
      listener.handleErrorMessage(header, message.getErrorMessage().getCode(),
          message.getErrorMessage().getDescription());
    }
  }

  /**
   * Verifies that the {@code serverToken} matches the token currently held by the
   * client (if any).
   */
  private boolean checkServerToken(ByteString serverToken) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    ByteString clientToken = listener.getClientToken();
    if (clientToken == null) {
      // No token. Return true so that we'll attempt to deliver a token control message (if any)
      // to the listener in handleIncomingMessage.
      return true;
    }

    if (!TypedUtil.<ByteString>equals(serverToken, clientToken)) {
      // Bad token - reject whole message.  However, our channel can send us messages intended for
      // other clients belonging to the same user, so don't log too loudly.
      logger.info("Incoming message has bad token: %s, %s",
          CommonProtoStrings2.toLazyCompactString(serverToken),
          CommonProtoStrings2.toLazyCompactString(clientToken));
      statistics.recordError(ClientErrorType.TOKEN_MISMATCH);
      return false;
    }
    return true;
  }

  /**
   * Sends a message to the server to request a client token.
   *
   * @param applicationClientId application-specific client id
   * @param nonce nonce for the request
   * @param debugString information to identify the caller
   */
  void sendInitializeMessage(ApplicationClientIdP applicationClientId, ByteString nonce,
      String debugString) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    // Simply store the message in pendingInitializeMessage and send it when the batching task runs.
    pendingInitializeMessage = CommonProtos2.newInitializeMessage(
        applicationClientId.getClientType(), applicationClientId, nonce,
        DigestSerializationType.BYTE_BASED);
    logger.info("Batching initialize message for client: %s, %s", debugString,
        pendingInitializeMessage);
    batchingTask.ensureScheduled(debugString);
  }

  /**
   * Sends an info message to the server with the performance counters supplied
   * in {@code performanceCounters} and the config supplies in
   * {@code configParams}.
   *
   * @param requestServerRegistrationSummary indicates whether to request the
   *        server's registration summary
   */
  void sendInfoMessage(List<SimplePair<String, Integer>> performanceCounters,
      ClientConfigP clientConfig, boolean requestServerRegistrationSummary) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    InfoMessage.Builder infoMessage = InfoMessage.newBuilder()
        .setClientVersion(clientVersion);

    // Add configuration parameters.
    if (clientConfig != null) {
      infoMessage.setClientConfig(clientConfig);
    }

    // Add performance counters.
    for (SimplePair<String, Integer> performanceCounter : performanceCounters) {
      PropertyRecord counter =
          CommonProtos2.newPropertyRecord(performanceCounter.first, performanceCounter.second);
      infoMessage.addPerformanceCounter(counter);
    }

    // Indicate whether we want the server's registration summary sent back.
    infoMessage.setServerRegistrationSummaryRequested(requestServerRegistrationSummary);

    // Simply store the message in pendingInfoMessage and send it when the batching task runs.
    pendingInfoMessage = infoMessage.build();
    batchingTask.ensureScheduled("Send-info");
  }

  /**
   * Sends a registration request to the server.
   *
   * @param objectIds object ids on which to (un)register
   * @param regOpType whether to register or unregister
   */
  void sendRegistrations(Collection<ObjectIdP> objectIds, RegistrationP.OpType regOpType) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    for (ObjectIdP objectId : objectIds) {
      pendingRegistrations.put(ProtoWrapper.of(objectId), regOpType);
    }
    batchingTask.ensureScheduled("Send-registrations");
  }

  /** Sends an acknowledgement for {@code invalidation} to the server. */
  void sendInvalidationAck(InvalidationP invalidation) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    // We could do squelching - we don't since it is unlikely to be too beneficial here.
    logger.fine("Sending ack for invalidation %s", invalidation);
    pendingAckedInvalidations.add(ProtoWrapper.of(invalidation));
    batchingTask.ensureScheduled("Send-Ack");
  }

  /**
   * Sends a single registration subtree to the server.
   *
   * @param regSubtree subtree to send
   */
  void sendRegistrationSyncSubtree(RegistrationSubtree regSubtree) {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    pendingRegSubtrees.add(ProtoWrapper.of(regSubtree));
    logger.info("Adding subtree: %s", regSubtree);
    batchingTask.ensureScheduled("Send-reg-sync");
 }

  /** Sends pending data to the server (e.g., registrations, acks, registration sync messages). */
  private void sendMessageToServer() {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");

    if (nextMessageSendTimeMs > internalScheduler.getCurrentTimeMs()) {
      logger.warning("In quiet period: not sending message to server: %s > %s",
          nextMessageSendTimeMs, internalScheduler.getCurrentTimeMs());
      return;
    }

    // Check if an initialize message needs to be sent.
    ClientToServerMessage.Builder builder = ClientToServerMessage.newBuilder();
    if (pendingInitializeMessage != null) {
      statistics.recordSentMessage(SentMessageType.INITIALIZE);
      builder.setInitializeMessage(pendingInitializeMessage);
      pendingInitializeMessage = null;
    }

    // Note: Even if an initialize message is being sent, we can send additional
    // messages such as regisration messages, etc to the server. But if there is no token
    // and an initialize message is not being sent, we cannot send any other message.

    if ((listener.getClientToken() == null) && !builder.hasInitializeMessage()) {
      // Cannot send any message
      logger.warning("Cannot send message since no token and no initialize msg: %s", builder);
      statistics.recordError(ClientErrorType.TOKEN_MISSING_FAILURE);
      return;
    }

    ClientHeader.Builder outgoingHeader = createClientHeader();
    builder.setHeader(outgoingHeader);

    // Check for pending batched operations and add to message builder if needed.

    // Add reg, acks, reg subtrees - clear them after adding.
    if (!pendingAckedInvalidations.isEmpty()) {
      builder.setInvalidationAckMessage(createInvalidationAckMessage());
      statistics.recordSentMessage(SentMessageType.INVALIDATION_ACK);
    }

    // Check regs.
    if (!pendingRegistrations.isEmpty()) {
      builder.setRegistrationMessage(createRegistrationMessage());
      statistics.recordSentMessage(SentMessageType.REGISTRATION);
    }

    // Check reg substrees.
    if (!pendingRegSubtrees.isEmpty()) {
      for (ProtoWrapper<RegistrationSubtree> subtree : pendingRegSubtrees) {
        builder.setRegistrationSyncMessage(RegistrationSyncMessage.newBuilder()
            .addSubtree(subtree.getProto()));
      }
      pendingRegSubtrees.clear();
      statistics.recordSentMessage(SentMessageType.REGISTRATION_SYNC);
    }

    // Check if an info message has to be sent.
    if (pendingInfoMessage != null) {
      statistics.recordSentMessage(SentMessageType.INFO);
      builder.setInfoMessage(pendingInfoMessage);
      pendingInfoMessage = null;
    }

    // Validate the message and send it.
    messageId++;
    ClientToServerMessage message = builder.build();
    if (!msgValidator.isValid(message)) {
      logger.severe("Tried to send invalid message: %s", message);
      statistics.recordError(ClientErrorType.OUTGOING_MESSAGE_FAILURE);
      return;
    }

    logger.fine("Sending message to server: %s", message);
    statistics.recordSentMessage(SentMessageType.TOTAL);
    network.sendMessage(message.toByteArray());

    // Record that the message was sent. Schedule it as a separate event to avoid invoking
    // a callback on the Ticl inline.
    internalScheduler.schedule(NO_DELAY, new Runnable() {
      @Override
      public void run() {
        listener.handleMessageSent();
      }
    });
  }

  /**
   * Creates a registration message based on registrations from {@code pendingRegistrations}
   * and returns it.
   * <p>
   * REQUIRES: pendingRegistrations.size() > 0
   */
  private RegistrationMessage createRegistrationMessage() {
    Preconditions.checkState(!pendingRegistrations.isEmpty());
    RegistrationMessage.Builder regMessage = RegistrationMessage.newBuilder();

    // Run through the pendingRegistrations map.
    for (Map.Entry<ProtoWrapper<ObjectIdP>, RegistrationP.OpType> entry :
         pendingRegistrations.entrySet()) {
      RegistrationP reg = CommonProtos2.newRegistrationP(entry.getKey().getProto(),
          entry.getValue() == RegistrationP.OpType.REGISTER);
      regMessage.addRegistration(reg);
    }
    pendingRegistrations.clear();
    return regMessage.build();
  }

  /**
   * Creates an invalidation ack message based on acks from {@code pendingAckedInvalidations} and
   * returns it.
   * <p>
   * REQUIRES: pendingAckedInvalidations.size() > 0
   */
  private InvalidationMessage createInvalidationAckMessage() {
    Preconditions.checkState(!pendingAckedInvalidations.isEmpty());
    InvalidationMessage.Builder ackMessage = InvalidationMessage.newBuilder();
    for (ProtoWrapper<InvalidationP> wrapper : pendingAckedInvalidations) {
      ackMessage.addInvalidation(wrapper.getProto());
    }
    pendingAckedInvalidations.clear();
    return ackMessage.build();
  }

  /** Returns the header to include on a message to the server. */
  private ClientHeader.Builder createClientHeader() {
    Preconditions.checkState(internalScheduler.isRunningOnThread(), "Not on internal thread");
    ClientHeader.Builder builder = ClientHeader.newBuilder()
        .setProtocolVersion(CommonInvalidationConstants2.PROTOCOL_VERSION)
        .setClientTimeMs(internalScheduler.getCurrentTimeMs())
        .setMessageId(Integer.toString(messageId))
        .setMaxKnownServerTimeMs(lastKnownServerTimeMs)
        .setRegistrationSummary(listener.getRegistrationSummary());
    ByteString clientToken = listener.getClientToken();
    if (clientToken != null) {
      logger.fine("Sending token on client->server message: %s",
          CommonProtoStrings2.toLazyCompactString(clientToken));
      builder.setClientToken(clientToken);
    }
    return builder;
  }
}
