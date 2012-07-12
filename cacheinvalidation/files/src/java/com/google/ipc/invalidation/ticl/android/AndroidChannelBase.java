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

package com.google.ipc.invalidation.ticl.android;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protos.ipc.invalidation.AndroidChannel.AddressedAndroidMessage;
import com.google.protos.ipc.invalidation.AndroidChannel.AddressedAndroidMessageBatch;

import android.net.http.AndroidHttpClient;

import org.apache.http.HttpEntity;
import org.apache.http.HttpResponse;
import org.apache.http.client.ClientProtocolException;
import org.apache.http.client.HttpClient;
import org.apache.http.client.ResponseHandler;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.entity.ByteArrayEntity;
import org.apache.http.impl.client.BasicResponseHandler;

import java.io.IOException;
import java.net.HttpURLConnection;
import java.util.List;


/**
 * Implementation of the HTTP communication used by {@code AndroidChannel}. Factored into
 * a separate class that can be run outside the Android environment to improve testing.
 *
 */

public abstract class AndroidChannelBase {
  /** Http client to use when making requests to . */
  private HttpClient httpClient;

  /** Authentication type for  frontends. */
  private final String authType;

  /** URL of the frontends. */
  private final String channelUrl;

  /** The token that will be echoed to the data center in the headers of all HTTP requests. */
  private String echoToken = null;

  /**
   * Creates an instance that uses {@code httpClient} to send requests to {@code channelUrl}
   * using an auth type of {@code authType}.
   */
  protected AndroidChannelBase(HttpClient httpClient, String authType, String channelUrl) {
    this.httpClient = httpClient;
    this.authType = authType;
    this.channelUrl = channelUrl;
  }

  /** Sends {@code outgoingMessage} to . */
  void deliverOutboundMessage(final byte[] outgoingMessage) {
    getLogger().fine("Delivering outbound message: %s bytes", outgoingMessage.length);
    StringBuilder target = new StringBuilder();

    // Build base URL that targets the inbound request service with the encoded network endpoint id
    target.append(channelUrl);
    target.append(AndroidHttpConstants.REQUEST_URL);
    target.append(getWebEncodedEndpointId());

    // Add query parameter indicating the service to authenticate against
    target.append('?');
    target.append(AndroidHttpConstants.SERVICE_PARAMETER);
    target.append('=');
    target.append(authType);

    // Construct entity containing the outbound protobuf msg
    ByteArrayEntity contentEntity = new ByteArrayEntity(outgoingMessage);
    contentEntity.setContentType(AndroidHttpConstants.PROTO_CONTENT_TYPE);

    // Construct POST request with the entity content and appropriate authorization
    HttpPost httpPost = new HttpPost(target.toString());
    httpPost.setEntity(contentEntity);
    setPostHeaders(httpPost);
    try {
      String response = httpClient.execute(httpPost, new BasicResponseHandler());
    } catch (ClientProtocolException exception) {
      // TODO: Distinguish between key HTTP error codes and handle more specifically
      // where appropriate.
      getLogger().warning("Error from server on request: %s", exception);
    } catch (IOException exception) {
      getLogger().warning("Error writing request: %s", exception);
    } catch (RuntimeException exception) {
      getLogger().warning("Runtime exception writing request: %s", exception);
    }
  }

  /** Retrieves any pending messages from  in the mailbox for the client. */
  
  public void retrieveMailbox() {
    // It's highly unlikely that we'll start receiving events before we have an auth token, but
    // if that is the case then we cannot retrieve mailbox contents.   The events should be
    // redelivered later.
    if (getAuthToken() == null) {
      getLogger().warning("Unable to retrieve mailbox. No auth token");
      return;
    }

    // Make a request to retrieve the mailbox contents.
    AddressedAndroidMessageBatch messageBatch = makeMailboxRequest();
    if (messageBatch == null) {
      // Logging already done in makeMailboxRequest.
      return;
    }
    List<AddressedAndroidMessage> msgList = messageBatch.getAddressedMessageList();
    getLogger().fine("Mailbox contains %s msgs", msgList.size());
    for (AddressedAndroidMessage message : msgList) {
      tryDeliverMessage(message);
    }
  }

  /**
   * Makes a POST request to the mailbox URL for this client. Returns the response as byte array,
   * or {@code null} if there was no response.
   */
  private AddressedAndroidMessageBatch makeMailboxRequest() {
    // Create URL that targets the mailbox retrieval service with the target mailbox id
    StringBuilder target = new StringBuilder();
    target.append(channelUrl);
    target.append(AndroidHttpConstants.MAILBOX_URL);
    target.append(getWebEncodedEndpointId());

    // Add query parameter indicating the service to authenticate against
    target.append('?');
    target.append(AndroidHttpConstants.SERVICE_PARAMETER);
    target.append('=');
    target.append(authType);

    HttpPost httpPost = new HttpPost(target.toString());
    setPostHeaders(httpPost);
    try {
      return httpClient.execute(httpPost, new ResponseHandler<AddressedAndroidMessageBatch>() {
        @Override
        public AddressedAndroidMessageBatch handleResponse(HttpResponse response)
            throws ClientProtocolException, IOException {
          if (response.getStatusLine().getStatusCode() == HttpURLConnection.HTTP_NO_CONTENT) {
            // No content in response.
            return null;
          }
          HttpEntity responseEntity = response.getEntity();
          if (responseEntity == null) {
            // There should have been content, but there wasn't.
            getLogger().warning("Missing response content");
            return null;
          }
          long contentLength = responseEntity.getContentLength();
          if ((contentLength < 0) || (contentLength > Integer.MAX_VALUE)) {
            getLogger().warning("Invalid mailbox Content-Length value: %s", contentLength);
            return null;
          }
          // If data present, read the mailbox data into a local byte array and return it.
          // A mailbox may be empty because a prior retrieval pulled down all messages available
          // (including the one that initiated the current C2DM message).
          if (contentLength == 0) {
            // We check separately from contentLength < 0 since 0 is a valid content-length, and
            // we don't want the log message to be confusing.
            return null;
          }
          // Parse and return the message batch. This function is allowed to throw an IOException,
          // so we don't need a try/catch.
          return AddressedAndroidMessageBatch.parseFrom(responseEntity.getContent());
        }
      });
    } catch (ClientProtocolException exception) {
      // TODO: Distinguish between key HTTP error codes and handle more specifically
      // where appropriate.
      getLogger().warning("Error from server on mailbox retrieval: %s", exception);
    } catch (InvalidProtocolBufferException exception) {
      getLogger().warning("Error parsing mailbox contents: %s", exception);
    } catch (IOException exception) {
      getLogger().warning("Error retrieving mailbox: %s", exception);
    } catch (RuntimeException exception) {
      getLogger().warning("Runtime exception retrieving mailbox: %s", exception);
    }
    return null;
  }

  /** Sets the Authorization and echo headers on {@code httpPost}. */
  private void setPostHeaders(HttpPost httpPost) {
    httpPost.setHeader("Authorization", "GoogleLogin auth=" + getAuthToken());
    if (echoToken != null) {
      // If we have a token to echo to the server, echo it.
      httpPost.setHeader(AndroidHttpConstants.ECHO_HEADER, echoToken);
    }
  }

  /**
   * If {@code echoToken} is not {@code null}, updates the token that will be sent in the header
   * of all HTTP requests.
   */
  void updateEchoToken(String echoToken) {
    if (echoToken != null) {
      this.echoToken = echoToken;
    }
  }

  /** Sets the HTTP client to {@code client}. */
  void setHttpClientForTest(HttpClient client) {
    if (this.httpClient instanceof AndroidHttpClient) {
      // Release the previous client if any.
      ((AndroidHttpClient) this.httpClient).close();
    }
    this.httpClient = Preconditions.checkNotNull(client);
  }

  /** Returns the base-64-encoded network endpoint id for the client. */
  protected abstract String getWebEncodedEndpointId();

  /** Returns the current authentication token for the client for web requests to . */
  protected abstract String getAuthToken();

  /** Returns the logger to use. */
  protected abstract Logger getLogger();

  /** Attempts to deliver a {@code message} from  to the local client. */
  protected abstract void tryDeliverMessage(AddressedAndroidMessage message);
}
