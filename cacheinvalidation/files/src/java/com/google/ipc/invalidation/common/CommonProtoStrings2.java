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

package com.google.ipc.invalidation.common;

import com.google.ipc.invalidation.util.Bytes;
import com.google.ipc.invalidation.util.TextBuilder;
import com.google.protobuf.ByteString;
import com.google.protos.ipc.invalidation.ClientProtocol.ApplicationClientIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientToServerMessage;
import com.google.protos.ipc.invalidation.ClientProtocol.InvalidationP;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationP;
import com.google.protos.ipc.invalidation.ClientProtocol.ServerToClientMessage;

import java.util.Collection;


/**
 * Utilities to make it easier/cleaner/shorter for printing and converting protobufs to strings in
 * . This class exposes methods to return objects such that their toString method returns a
 * compact representation of the proto. These methods can be used in the Ticl
 *
 */
public class CommonProtoStrings2 {

  //
  // Implementation notes: The following methods return the object as mentioned above for different
  // protos. Each method (except for a couple of them) essentially calls a private static method
  // that uses a TextBuilder to construct the final string. Each method has the following spec:
  // Returns a compact string representation for {@code <parameter-name>} for logging
  //

  /** See spec in implementation notes. */
  public static Object toLazyCompactString(final ByteString byteString) {
    if (byteString == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        return Bytes.toString(byteString.toByteArray());
      }
    };
  }

  /** See spec in implementation notes. */
  public static Object toLazyCompactString(final byte[] bytes) {
    if (bytes == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        return Bytes.toString(bytes);
      }
    };
  }

  /** See spec in implementation notes. */
  public static Object toLazyCompactString(final ObjectIdP objectId) {
    if (objectId == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        return toCompactString(new TextBuilder(), objectId).toString();
      }
    };
  }

  /** See spec in implementation notes. */
  public static Object toLazyCompactString(final InvalidationP invalidation) {
    if (invalidation == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        return toCompactString(new TextBuilder(), invalidation).toString();
      }
    };
  }

 /** See spec in implementation notes. */
  public static Object toLazyCompactString(final ApplicationClientIdP applicationId) {
    if (applicationId == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        return toCompactString(new TextBuilder(), applicationId).toString();
      }
    };
  }

  /** See spec in implementation notes. */
  public static Object toLazyCompactStringForObjectIds(
      final Collection<ObjectIdP> objectIds) {
    if (objectIds == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        TextBuilder builder = new TextBuilder();
        boolean first = true;
        for (ObjectIdP objectId : objectIds) {
          if (!first) {
            builder.append(", ");
          }
          toCompactString(builder, objectId);
          first = false;
        }
        return builder.toString();
      }
    };
  }

  /** See spec in implementation notes. */
  public static Object toLazyCompactStringForInvalidations(
      final Collection<InvalidationP> invalidations) {
    if (invalidations == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        TextBuilder builder = new TextBuilder();
        boolean first = true;
        for (InvalidationP invalidation : invalidations) {
          if (!first) {
            builder.append(", ");
          }
          toCompactString(builder, invalidation);
          first = false;
        }
        return builder.toString();
      }
    };
  }

  /** See spec in implementation notes. */
  public static Object toLazyCompactStringForRegistrations(
      final Collection<RegistrationP> registrations) {
    if (registrations == null) {
      return null;
    }
    return new Object() {
      @Override
      public String toString() {
        TextBuilder builder = new TextBuilder();
        boolean first = true;
        for (RegistrationP registration : registrations) {
          if (!first) {
            builder.append(", ");
          }
          toCompactString(builder, registration);
          first = false;
        }
        return builder.toString();
      }
    };
  }

  //
  // Implementation notes: The following helper methods do the actual conversion of the proto into
  // the compact representation in the given builder. Each method has the following spec:
  // Adds a compact representation for {@code <parameter-name>} to {@code builder} and
  // returns {@code builder}.
  // TODO: Look into building indirection tables for the collections to avoid
  // code duplication.
  //

  /** See spec in implementation notes. */
  public static TextBuilder toCompactString(TextBuilder builder, ByteString byteString) {
    return Bytes.toCompactString(builder, byteString.toByteArray());
  }

  /** See spec in implementation notes. */
  public static TextBuilder toCompactString(TextBuilder builder, ObjectIdP objectId) {
    builder.appendFormat("(Obj: %s, ", objectId.getSource());
    toCompactString(builder, objectId.getName());
    builder.append(')');
    return builder;
  }

  /** See spec in implementation notes. */
  public static TextBuilder toCompactString(TextBuilder builder, InvalidationP invalidation) {
    builder.append("(Inv: ");
    toCompactString(builder, invalidation.getObjectId());
    builder.append(", ");
    builder.append(invalidation.getVersion());
    if (invalidation.hasPayload()) {
      builder.append(", P:");
      toCompactString(builder, invalidation.getPayload());
    }
    builder.append(')');
    return builder;
  }

  /** See spec in implementation notes. */
  public static TextBuilder toCompactString(TextBuilder builder, RegistrationP regOp) {
    builder.appendFormat("RegOp: %s, Obj = ",
        regOp.getOpType() == RegistrationP.OpType.REGISTER ? "R" : "U");
    toCompactString(builder, regOp.getObjectId());
    return builder;
  }

  /** See spec in implementation notes. */
  private static TextBuilder toCompactString(TextBuilder builder,
      ApplicationClientIdP applicationClientId) {
    builder.appendFormat("(Ceid: ");
    toCompactString(builder, applicationClientId.getClientName());
    builder.append(')');
    return builder;
  }

  public static String toCompactString(ClientToServerMessage msg) {
    return "CSMessage: <" + msg.getHeader().getMessageId() + ">";
  }

  public static String toCompactString(ServerToClientMessage msg) {
    return "SCMessage: <" + msg.getHeader().getMessageId() + ">";
  }

  private CommonProtoStrings2() { // To prevent instantiation
  }
}
