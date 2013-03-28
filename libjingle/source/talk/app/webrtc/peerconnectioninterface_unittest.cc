/*
 * libjingle
 * Copyright 2012, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string>

#include "talk/app/webrtc/fakeportallocatorfactory.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/localvideosource.h"
#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/app/webrtc/test/fakeconstraints.h"
#include "talk/app/webrtc/test/mockpeerconnectionobservers.h"
#include "talk/base/gunit.h"
#include "talk/base/scoped_ptr.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/media/base/fakevideocapturer.h"
#include "talk/session/media/mediasession.h"

static const char kStreamLabel1[] = "local_stream_1";
static const char kStreamLabel2[] = "local_stream_2";
static const char kStreamLabel3[] = "local_stream_3";
static const int kDefaultStunPort = 3478;
static const char kStunAddressOnly[] = "stun:address";
static const char kStunInvalidPort[] = "stun:address:-1";
static const char kStunAddressPortAndMore1[] = "stun:address:port:more";
static const char kStunAddressPortAndMore2[] = "stun:address:port more";
static const char kTurnIceServerUri[] = "turn:user@turn.example.org";
static const char kTurnUsername[] = "user";
static const char kTurnPassword[] = "password";
static const char kTurnHostname[] = "turn.example.org";
static const uint32 kTimeout = 5000U;

using talk_base::scoped_ptr;
using talk_base::scoped_refptr;
using webrtc::AudioSourceInterface;
using webrtc::AudioTrackInterface;
using webrtc::DataChannelInterface;
using webrtc::FakeConstraints;
using webrtc::FakePortAllocatorFactory;
using webrtc::IceCandidateInterface;

using webrtc::DataBuffer;
using webrtc::JsepInterface;
using webrtc::LocalMediaStreamInterface;
using webrtc::MediaStreamInterface;
using webrtc::MediaStreamTrackInterface;
using webrtc::MockCreateSessionDescriptionObserver;
using webrtc::MockDataChannelObserver;
using webrtc::MockSetSessionDescriptionObserver;
using webrtc::PeerConnectionInterface;
using webrtc::PeerConnectionObserver;
using webrtc::PortAllocatorFactoryInterface;
using webrtc::SessionDescriptionInterface;
using webrtc::VideoSourceInterface;
using webrtc::VideoTrackInterface;

namespace {

// Gets the first ssrc of given content type from the ContentInfo.
bool GetFirstSsrc(const cricket::ContentInfo* content_info, int* ssrc) {
  if (!content_info || !ssrc) {
    return false;
  }
  const cricket::MediaContentDescription* media_desc =
      static_cast<const cricket::MediaContentDescription*> (
          content_info->description);
  if (!media_desc || media_desc->streams().empty()) {
    return false;
  }
  *ssrc = media_desc->streams().begin()->first_ssrc();
  return true;
}

void SetSsrcToZero(std::string* sdp) {
  const char kSdpSsrcAtribute[] = "a=ssrc:";
  const char kSdpSsrcAtributeZero[] = "a=ssrc:0";
  size_t ssrc_pos = 0;
  while ((ssrc_pos = sdp->find(kSdpSsrcAtribute, ssrc_pos)) !=
      std::string::npos) {
    size_t end_ssrc = sdp->find(" ", ssrc_pos);
    sdp->replace(ssrc_pos, end_ssrc - ssrc_pos, kSdpSsrcAtributeZero);
    ssrc_pos = end_ssrc;
  }
}

class MockPeerConnectionObserver : public PeerConnectionObserver {
 public:
  MockPeerConnectionObserver()
      : renegotiation_needed_(false),
        ice_complete_(false) {
  }
  ~MockPeerConnectionObserver() {
  }
  void SetPeerConnectionInterface(PeerConnectionInterface* pc) {
    pc_ = pc;
    state_ = pc_->ready_state();
  }
  virtual void OnError() {}
  virtual void OnStateChange(StateType state_changed) {
    if (pc_.get() == NULL)
      return;
    switch (state_changed) {
      case kReadyState:
        state_ = pc_->ready_state();
        break;
      case kIceState:
        ADD_FAILURE();
        break;
      default:
        ADD_FAILURE();
        break;
    }
  }
  virtual void OnAddStream(MediaStreamInterface* stream) {
    last_added_stream_ = stream;
  }
  virtual void OnRemoveStream(MediaStreamInterface* stream) {
    last_removed_stream_ = stream;
  }
  virtual void OnRenegotiationNeeded() {
    renegotiation_needed_ = true;
  }
  virtual void OnDataChannel(DataChannelInterface* data_channel) {
    last_datachannel_ = data_channel;
  }

  virtual void OnIceChange() {}
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    std::string sdp;
    EXPECT_TRUE(candidate->ToString(&sdp));
    EXPECT_LT(0u, sdp.size());
    last_candidate_.reset(webrtc::CreateIceCandidate(candidate->sdp_mid(),
        candidate->sdp_mline_index(), sdp));
    EXPECT_TRUE(last_candidate_.get() != NULL);
  }
  virtual void OnIceComplete() {
    ice_complete_ = true;
  }

  // Returns the label of the last added stream.
  // Empty string if no stream have been added.
  std::string GetLastAddedStreamLabel() {
    if (last_added_stream_.get())
      return last_added_stream_->label();
    return "";
  }
  std::string GetLastRemovedStreamLabel() {
    if (last_removed_stream_.get())
      return last_removed_stream_->label();
    return "";
  }

  scoped_refptr<PeerConnectionInterface> pc_;
  PeerConnectionInterface::ReadyState state_;
  scoped_ptr<IceCandidateInterface> last_candidate_;
  scoped_refptr<DataChannelInterface> last_datachannel_;
  bool renegotiation_needed_;
  bool ice_complete_;

 private:
  scoped_refptr<MediaStreamInterface> last_added_stream_;
  scoped_refptr<MediaStreamInterface> last_removed_stream_;
};

// TODO(perkj): Move to a common file with test mocks.
class MockStatsObserver : public webrtc::StatsObserver {
 public:
  MockStatsObserver()
      : called_(false),
        number_of_reports_(0) {}
  virtual ~MockStatsObserver() {}
  virtual void OnComplete(const std::vector<webrtc::StatsReport>& reports) {
    called_ = true;
    number_of_reports_ = reports.size();
  }

  bool called() const { return called_; }
  size_t number_of_reports() const { return number_of_reports_; }

 private:
  bool called_;
  size_t number_of_reports_;
};

}  // namespace
class PeerConnectionInterfaceTest : public testing::Test {
 protected:
  virtual void SetUp() {
    pc_factory_ = webrtc::CreatePeerConnectionFactory(
        talk_base::Thread::Current(), talk_base::Thread::Current(), NULL);
    ASSERT_TRUE(pc_factory_.get() != NULL);
  }

  void CreatePeerConnection() {
    CreatePeerConnection("", "", NULL);
  }

  void CreatePeerConnection(webrtc::MediaConstraintsInterface* constraints) {
    CreatePeerConnection("", "", constraints);
  }

  void CreatePeerConnection(const std::string& uri,
                            const std::string& password,
                            webrtc::MediaConstraintsInterface* constraints) {
    JsepInterface::IceServer server;
    JsepInterface::IceServers servers;
    server.uri = uri;
    server.password = password;
    servers.push_back(server);

    port_allocator_factory_ = FakePortAllocatorFactory::Create();
    pc_ = pc_factory_->CreatePeerConnection(servers, constraints,
                                            port_allocator_factory_.get(),
                                            &observer_);
    ASSERT_TRUE(pc_.get() != NULL);
    observer_.SetPeerConnectionInterface(pc_.get());
    EXPECT_EQ(PeerConnectionInterface::kNew, observer_.state_);
  }

  void CreatePeerConnectionWithDifferentConfigurations() {
    CreatePeerConnection(kStunAddressOnly, "", NULL);
    EXPECT_EQ(1u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());
    EXPECT_EQ("address",
        port_allocator_factory_->stun_configs()[0].server.hostname());
    EXPECT_EQ(kDefaultStunPort,
        port_allocator_factory_->stun_configs()[0].server.port());

    CreatePeerConnection(kStunInvalidPort, "", NULL);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    CreatePeerConnection(kStunAddressPortAndMore1, "", NULL);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    CreatePeerConnection(kStunAddressPortAndMore2, "", NULL);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());

    CreatePeerConnection(kTurnIceServerUri, kTurnPassword, NULL);
    EXPECT_EQ(1u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(1u, port_allocator_factory_->turn_configs().size());
    EXPECT_EQ(kTurnUsername,
              port_allocator_factory_->turn_configs()[0].username);
    EXPECT_EQ(kTurnPassword,
              port_allocator_factory_->turn_configs()[0].password);
    EXPECT_EQ(kTurnHostname,
              port_allocator_factory_->turn_configs()[0].server.hostname());
    EXPECT_EQ(kTurnHostname,
              port_allocator_factory_->stun_configs()[0].server.hostname());
  }

  void AddStream(const std::string& label) {
    // Create a local stream.
    scoped_refptr<LocalMediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(label));
    scoped_refptr<VideoSourceInterface> video_source(
        pc_factory_->CreateVideoSource(new cricket::FakeVideoCapturer(), NULL));
    scoped_refptr<VideoTrackInterface> video_track(
        pc_factory_->CreateVideoTrack(label, video_source));
    stream->AddTrack(video_track.get());
    EXPECT_TRUE(pc_->AddStream(stream, NULL));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  void AddVoiceStream(const std::string& label) {
    // Create a local stream.
    scoped_refptr<LocalMediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(label));
    scoped_refptr<AudioTrackInterface> audio_track(
        pc_factory_->CreateAudioTrack(label, NULL));
    stream->AddTrack(audio_track.get());
    EXPECT_TRUE(pc_->AddStream(stream, NULL));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  void AddAudioVideoStream(const std::string& stream_label,
                           const std::string& audio_track_label,
                           const std::string& video_track_label) {
    // Create a local stream.
    scoped_refptr<LocalMediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(stream_label));
    scoped_refptr<AudioTrackInterface> audio_track(
        pc_factory_->CreateAudioTrack(
            audio_track_label, static_cast<AudioSourceInterface*>(NULL)));
    stream->AddTrack(audio_track.get());
    scoped_refptr<VideoTrackInterface> video_track(
        pc_factory_->CreateVideoTrack(video_track_label, NULL));
    stream->AddTrack(video_track.get());
    EXPECT_TRUE(pc_->AddStream(stream, NULL));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  bool DoCreateOfferAnswer(SessionDescriptionInterface** desc, bool offer) {
    talk_base::scoped_refptr<MockCreateSessionDescriptionObserver>
        observer(new talk_base::RefCountedObject<
            MockCreateSessionDescriptionObserver>());
    if (offer) {
      pc_->CreateOffer(observer, NULL);
    } else {
      pc_->CreateAnswer(observer, NULL);
    }
    EXPECT_EQ_WAIT(true, observer->called(), kTimeout);
    *desc = observer->release_desc();
    return observer->result();
  }

  bool DoCreateOffer(SessionDescriptionInterface** desc) {
    return DoCreateOfferAnswer(desc, true);
  }

  bool DoCreateAnswer(SessionDescriptionInterface** desc) {
    return DoCreateOfferAnswer(desc, false);
  }

  bool DoSetSessionDescription(SessionDescriptionInterface* desc, bool local) {
    talk_base::scoped_refptr<MockSetSessionDescriptionObserver>
        observer(new talk_base::RefCountedObject<
            MockSetSessionDescriptionObserver>());
    if (local) {
      pc_->SetLocalDescription(observer, desc);
    } else {
      pc_->SetRemoteDescription(observer, desc);
    }
    EXPECT_EQ_WAIT(true, observer->called(), kTimeout);
    return observer->result();
  }

  bool DoSetLocalDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, true);
  }

  bool DoSetRemoteDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, false);
  }

  // Calls PeerConnection::GetStats and check the return value.
  // It does not verify the values in the StatReports since a RTCP packet might
  // be required.
  bool DoGetStats(MediaStreamTrackInterface* track) {
    talk_base::scoped_refptr<MockStatsObserver> observer(
        new talk_base::RefCountedObject<MockStatsObserver>());
    if (!pc_->GetStats(observer, track))
      return false;
    EXPECT_TRUE_WAIT(observer->called(), kTimeout);
    return observer->called();
  }

  void InitiateCall() {
    CreatePeerConnection();
    // Create a local stream with audio&video tracks.
    AddAudioVideoStream(kStreamLabel1, "audio_label", "video_label");
    CreateOfferReceiveAnswer();
  }

  void ReceiveOfferCreateAnswer() {
    bool first_negotiate = pc_->local_description() == NULL;
    SessionDescriptionInterface* offer = NULL;
    EXPECT_TRUE(DoCreateOffer(&offer));
    EXPECT_TRUE(DoSetRemoteDescription(offer));

    if (first_negotiate)
      EXPECT_EQ(PeerConnectionInterface::kOpening, observer_.state_);
    else
      EXPECT_EQ(PeerConnectionInterface::kActive, observer_.state_);

    SessionDescriptionInterface* answer = NULL;
    EXPECT_TRUE(DoCreateAnswer(&answer));
    EXPECT_TRUE(DoSetLocalDescription(answer));
    EXPECT_EQ(PeerConnectionInterface::kActive, observer_.state_);
  }

  void CreateOfferReceiveAnswer() {
    CreateOfferAsLocalDescription();
    std::string sdp;
    EXPECT_TRUE(pc_->local_description()->ToString(&sdp));
    CreateAnswerAsRemoteDescription(sdp);
  }

  void CreateOfferAsLocalDescription() {
    SessionDescriptionInterface* offer = NULL;
    ASSERT_TRUE(DoCreateOffer(&offer));
    EXPECT_TRUE(DoSetLocalDescription(offer));
    EXPECT_EQ(PeerConnectionInterface::kOpening, observer_.state_);
  }

  void CreateAnswerAsRemoteDescription(const std::string& offer) {
    webrtc::JsepSessionDescription* answer = new webrtc::JsepSessionDescription(
        SessionDescriptionInterface::kAnswer);
    EXPECT_TRUE(answer->Initialize(offer));
    EXPECT_TRUE(DoSetRemoteDescription(answer));
    EXPECT_EQ(PeerConnectionInterface::kActive, observer_.state_);
  }

  // Creates an offer and applies it as a local session description.
  // Creates an answer with the same SDP an the offer but removes all lines
  // that start with a:ssrc"
  void CreateOfferReceiveAnswerWithoutSsrc() {
    CreateOfferAsLocalDescription();
    std::string sdp;
    EXPECT_TRUE(pc_->local_description()->ToString(&sdp));
    SetSsrcToZero(&sdp);
    CreateAnswerAsRemoteDescription(sdp);
  }

  scoped_refptr<FakePortAllocatorFactory> port_allocator_factory_;
  scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;
  scoped_refptr<PeerConnectionInterface> pc_;
  MockPeerConnectionObserver observer_;
};

TEST_F(PeerConnectionInterfaceTest,
       CreatePeerConnectionWithDifferentConfigurations) {
  CreatePeerConnectionWithDifferentConfigurations();
}

TEST_F(PeerConnectionInterfaceTest, AddStreams) {
  CreatePeerConnection();
  AddStream(kStreamLabel1);
  AddVoiceStream(kStreamLabel2);
  ASSERT_EQ(2u, pc_->local_streams()->count());

  // Fail to add another stream with audio since we already have an audio track.
  scoped_refptr<LocalMediaStreamInterface> stream(
      pc_factory_->CreateLocalMediaStream(kStreamLabel3));
  scoped_refptr<AudioTrackInterface> audio_track(
      pc_factory_->CreateAudioTrack(
          kStreamLabel3, static_cast<AudioSourceInterface*>(NULL)));
  stream->AddTrack(audio_track.get());
  EXPECT_FALSE(pc_->AddStream(stream, NULL));

  // Remove the stream with the audio track.
  pc_->RemoveStream(pc_->local_streams()->at(1));

  // Test that we now can add the stream with the audio track.
  EXPECT_TRUE(pc_->AddStream(stream, NULL));
}

TEST_F(PeerConnectionInterfaceTest, RemoveStream) {
  CreatePeerConnection();
  AddStream(kStreamLabel1);
  ASSERT_EQ(1u, pc_->local_streams()->count());
  pc_->RemoveStream(pc_->local_streams()->at(0));
  EXPECT_EQ(0u, pc_->local_streams()->count());
}

TEST_F(PeerConnectionInterfaceTest, CreateOfferReceiveAnswer) {
  InitiateCall();
  // Since we answer with the same session description as we offer we can
  // check if OnAddStream have been called.
  EXPECT_EQ_WAIT(kStreamLabel1, observer_.GetLastAddedStreamLabel(), kTimeout);
}

TEST_F(PeerConnectionInterfaceTest, ReceiveOfferCreateAnswer) {
  CreatePeerConnection();
  AddStream(kStreamLabel1);

  ReceiveOfferCreateAnswer();

  // Since we answer with the same session description as we offer we can
  // check if OnAddStream have been called.
  EXPECT_EQ_WAIT(kStreamLabel1, observer_.GetLastAddedStreamLabel(), kTimeout);
}

TEST_F(PeerConnectionInterfaceTest, Renegotiate) {
  InitiateCall();
  ASSERT_EQ(1u, pc_->remote_streams()->count());
  pc_->RemoveStream(pc_->local_streams()->at(0));
  CreateOfferReceiveAnswer();
  EXPECT_EQ(0u, pc_->remote_streams()->count());
  AddStream(kStreamLabel1);
  CreateOfferReceiveAnswer();
}

// Test that candidates are generated and that we can parse our own candidates.
TEST_F(PeerConnectionInterfaceTest, IceCandidates) {
  CreatePeerConnection();

  EXPECT_FALSE(pc_->AddIceCandidate(observer_.last_candidate_.get()));
  // SetRemoteDescription takes ownership of offer.
  SessionDescriptionInterface* offer = NULL;
  AddStream(kStreamLabel1);
  EXPECT_TRUE(DoCreateOffer(&offer));
  EXPECT_TRUE(DoSetRemoteDescription(offer));

  // SetLocalDescription takes ownership of answer.
  SessionDescriptionInterface* answer = NULL;
  EXPECT_TRUE(DoCreateAnswer(&answer));
  EXPECT_TRUE(DoSetLocalDescription(answer));

  EXPECT_TRUE_WAIT(observer_.last_candidate_.get() != NULL, kTimeout);
  EXPECT_TRUE_WAIT(observer_.ice_complete_, kTimeout);

  EXPECT_TRUE(pc_->AddIceCandidate(observer_.last_candidate_.get()));
}

// Test that the CreateOffer and CreatAnswer will fail if the track labels are
// not unique.
TEST_F(PeerConnectionInterfaceTest, CreateOfferAnswerWithInvalidStream) {
  CreatePeerConnection();
  // Create a regular offer for the CreateAnswer test later.
  SessionDescriptionInterface* offer = NULL;
  EXPECT_TRUE(DoCreateOffer(&offer));
  EXPECT_TRUE(offer != NULL);
  delete offer;
  offer = NULL;

  // Create a local stream with audio&video tracks having same label.
  AddAudioVideoStream(kStreamLabel1, "track_label", "track_label");

  // Test CreateOffer
  EXPECT_FALSE(DoCreateOffer(&offer));

  // Test CreateAnswer
  SessionDescriptionInterface* answer = NULL;
  EXPECT_FALSE(DoCreateAnswer(&answer));
}

// Test that we will get different SSRCs for each tracks in the offer and answer
// we created.
TEST_F(PeerConnectionInterfaceTest, SsrcInOfferAnswer) {
  CreatePeerConnection();
  // Create a local stream with audio&video tracks having different labels.
  AddAudioVideoStream(kStreamLabel1, "audio_label", "video_label");

  // Test CreateOffer
  scoped_ptr<SessionDescriptionInterface> offer;
  EXPECT_TRUE(DoCreateOffer(offer.use()));
  int audio_ssrc = 0;
  int video_ssrc = 0;
  EXPECT_TRUE(GetFirstSsrc(GetFirstAudioContent(offer->description()),
                           &audio_ssrc));
  EXPECT_TRUE(GetFirstSsrc(GetFirstVideoContent(offer->description()),
                           &video_ssrc));
  EXPECT_NE(audio_ssrc, video_ssrc);

  // Test CreateAnswer
  EXPECT_TRUE(DoSetRemoteDescription(offer.release()));
  scoped_ptr<SessionDescriptionInterface> answer;
  EXPECT_TRUE(DoCreateAnswer(answer.use()));
  audio_ssrc = 0;
  video_ssrc = 0;
  EXPECT_TRUE(GetFirstSsrc(GetFirstAudioContent(answer->description()),
                           &audio_ssrc));
  EXPECT_TRUE(GetFirstSsrc(GetFirstVideoContent(answer->description()),
                           &video_ssrc));
  EXPECT_NE(audio_ssrc, video_ssrc);
}

// Test that we can specify a certain track that we want statistics about.
TEST_F(PeerConnectionInterfaceTest, GetStatsForSpecificTrack) {
  InitiateCall();
  ASSERT_LT(0u, pc_->remote_streams()->count());
  ASSERT_LT(0u, pc_->remote_streams()->at(0)->audio_tracks()->count());
  scoped_refptr<MediaStreamTrackInterface> remote_audio =
      pc_->remote_streams()->at(0)->audio_tracks()->at(0);
  EXPECT_TRUE(DoGetStats(remote_audio));

  // Remove the stream. Since we are sending to our selves the local
  // and the remote stream is the same.
  pc_->RemoveStream(pc_->local_streams()->at(0));
  // Do a re-negotiation.
  CreateOfferReceiveAnswer();

  ASSERT_EQ(0u, pc_->remote_streams()->count());

  // Test that we still can get statistics for the old track. Even if it is not
  // sent any longer.
  EXPECT_TRUE(DoGetStats(remote_audio));
}

// Test that we don't get statistics for an invalid track.
TEST_F(PeerConnectionInterfaceTest, GetStatsForInvalidTrack) {
  InitiateCall();
  scoped_refptr<AudioTrackInterface> unknown_audio_track(
      pc_factory_->CreateAudioTrack("unknown track", NULL));
  EXPECT_FALSE(DoGetStats(unknown_audio_track));
}

// This test setup two RTP data channels in loop back.
#ifdef WIN32
// TODO(perkj): Investigate why the transport channel sometimes don't become
// writable on Windows when we try to connect in loop back.
TEST_F(PeerConnectionInterfaceTest, DISABLED_TestDataChannel) {
#else
TEST_F(PeerConnectionInterfaceTest, TestDataChannel) {
#endif
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);
  scoped_refptr<DataChannelInterface> data1  =
      pc_->CreateDataChannel("test1", NULL);
  scoped_refptr<DataChannelInterface> data2  =
      pc_->CreateDataChannel("test2", NULL);
  ASSERT_TRUE(data1 != NULL);
  talk_base::scoped_ptr<MockDataChannelObserver> observer1(
      new MockDataChannelObserver(data1));
  talk_base::scoped_ptr<MockDataChannelObserver> observer2(
      new MockDataChannelObserver(data2));

  EXPECT_EQ(DataChannelInterface::kConnecting, data1->state());
  EXPECT_EQ(DataChannelInterface::kConnecting, data2->state());
  std::string data_to_send1 = "testing testing";
  std::string data_to_send2 = "testing something else";
  EXPECT_FALSE(data1->Send(DataBuffer(data_to_send1)));

  CreateOfferReceiveAnswer();
  EXPECT_TRUE_WAIT(observer1->IsOpen(), kTimeout);
  EXPECT_TRUE_WAIT(observer2->IsOpen(), kTimeout);

  EXPECT_EQ(DataChannelInterface::kOpen, data1->state());
  EXPECT_EQ(DataChannelInterface::kOpen, data2->state());
  EXPECT_TRUE(data1->Send(DataBuffer(data_to_send1)));
  EXPECT_TRUE(data2->Send(DataBuffer(data_to_send2)));

  EXPECT_EQ_WAIT(data_to_send1, observer1->last_message(), kTimeout);
  EXPECT_EQ_WAIT(data_to_send2, observer2->last_message(), kTimeout);

  data1->Close();
  EXPECT_EQ(DataChannelInterface::kClosing, data1->state());
  CreateOfferReceiveAnswer();
  EXPECT_FALSE(observer1->IsOpen());
  EXPECT_EQ(DataChannelInterface::kClosed, data1->state());
  EXPECT_TRUE(observer2->IsOpen());

  data_to_send2 = "testing something else again";
  EXPECT_TRUE(data2->Send(DataBuffer(data_to_send2)));

  EXPECT_EQ_WAIT(data_to_send2, observer2->last_message(), kTimeout);
}

// This test setup a RTP data channels in loop back and test that a channel is
// opened even if the remote end answer with a zero SSRC.
#ifdef WIN32
// TODO(perkj): Investigate why the transport channel sometimes don't become
// writable on Windows when we try to connect in loop back.
TEST_F(PeerConnectionInterfaceTest, DISABLED_TestSendOnlyDataChannel) {
#else
TEST_F(PeerConnectionInterfaceTest, TestSendOnlyDataChannel) {
#endif
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);
  scoped_refptr<DataChannelInterface> data1  =
      pc_->CreateDataChannel("test1", NULL);
  talk_base::scoped_ptr<MockDataChannelObserver> observer1(
      new MockDataChannelObserver(data1));

  CreateOfferReceiveAnswerWithoutSsrc();

  EXPECT_TRUE_WAIT(observer1->IsOpen(), kTimeout);

  data1->Close();
  EXPECT_EQ(DataChannelInterface::kClosing, data1->state());
  CreateOfferReceiveAnswerWithoutSsrc();
  EXPECT_EQ(DataChannelInterface::kClosed, data1->state());
  EXPECT_FALSE(observer1->IsOpen());
}

// This test that if a data channel is added in an answer a receive only channel
// channel is created.
TEST_F(PeerConnectionInterfaceTest, TestReceiveOnlyDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  std::string offer_label = "offer_channel";
  scoped_refptr<DataChannelInterface> offer_channel  =
      pc_->CreateDataChannel(offer_label, NULL);

  CreateOfferAsLocalDescription();

  // Replace the data channel label in the offer and apply it as an answer.
  std::string receive_label = "answer_channel";
  std::string sdp;
  EXPECT_TRUE(pc_->local_description()->ToString(&sdp));
  talk_base::replace_substrs(offer_label.c_str(), offer_label.length(),
                             receive_label.c_str(), receive_label.length(),
                             &sdp);
  CreateAnswerAsRemoteDescription(sdp);

  // Verify that a new incoming data channel has been created and that
  // it is open but can't we written to.
  ASSERT_TRUE(observer_.last_datachannel_ != NULL);
  DataChannelInterface* received_channel = observer_.last_datachannel_;
  EXPECT_EQ(DataChannelInterface::kConnecting, received_channel->state());
  EXPECT_EQ(receive_label, received_channel->label());
  EXPECT_FALSE(received_channel->Send(DataBuffer("something")));

  // Verify that the channel we initially offered has been rejected.
  EXPECT_EQ(DataChannelInterface::kClosed, offer_channel->state());

  // Do another offer / answer exchange and verify that the data channel is
  // opened.
  CreateOfferReceiveAnswer();
  EXPECT_EQ_WAIT(DataChannelInterface::kOpen, received_channel->state(),
                 kTimeout);
}

// This test that no data channel is returned if a reliable channel is
// requested.
// TODO(perkj): Remove this test once reliable channels are implemented.
TEST_F(PeerConnectionInterfaceTest, CreateReliableDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  std::string label = "test";
  webrtc::DataChannelInit config;
  config.reliable = true;
  scoped_refptr<DataChannelInterface> channel  =
      pc_->CreateDataChannel(label, &config);
  EXPECT_TRUE(channel == NULL);
}


