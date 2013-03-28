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

#include "talk/app/webrtc/localvideosource.h"

#include <string>
#include <vector>

#include "talk/base/gunit.h"
#include "talk/media/base/fakemediaengine.h"
#include "talk/media/base/fakevideorenderer.h"
#include "talk/media/devices/fakedevicemanager.h"
#include "talk/session/media/channelmanager.h"

using webrtc::LocalVideoSource;
using webrtc::MediaConstraintsInterface;
using webrtc::MediaSourceInterface;
using webrtc::ObserverInterface;
using webrtc::VideoSourceInterface;

namespace {

// Max wait time for a test.
const int kMaxWaitMs = 100;

}  // anonymous namespace


// TestVideoCapturer extends cricket::FakeVideoCapturer so it can be used for
// testing without known camera formats.
// It keeps its own lists of cricket::VideoFormats for the unit tests in this
// file.
class TestVideoCapturer : public cricket::FakeVideoCapturer {
 public:
  TestVideoCapturer() : test_without_formats_(false) {
    std::vector<cricket::VideoFormat> formats;
    formats.push_back(cricket::VideoFormat(1280, 720,
        cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(640, 480,
        cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(640, 400,
            cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(320, 240,
        cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(352, 288,
            cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    ResetSupportedFormats(formats);
  }

  // This function is used for resetting the supported capture formats and
  // simulating a cricket::VideoCapturer implementation that don't support
  // capture format enumeration. This is used to simulate the current
  // Chrome implementation.
  void TestWithoutCameraFormats() {
    test_without_formats_ = true;
    std::vector<cricket::VideoFormat> formats;
    ResetSupportedFormats(formats);
  }

  virtual cricket::CaptureState Start(
      const cricket::VideoFormat& capture_format) {
    if (test_without_formats_) {
      std::vector<cricket::VideoFormat> formats;
      formats.push_back(capture_format);
      ResetSupportedFormats(formats);
    }
    return FakeVideoCapturer::Start(capture_format);
  }

  virtual bool GetBestCaptureFormat(const cricket::VideoFormat& desired,
                                    cricket::VideoFormat* best_format) {
    if (test_without_formats_) {
      *best_format = desired;
      return true;
    }
    return FakeVideoCapturer::GetBestCaptureFormat(desired,
                                                   best_format);
  }

 private:
  bool test_without_formats_;
};


class TestConstraints : public MediaConstraintsInterface {
 public:
  void AddMandatory(const std::string& key, const std::string& value) {
    mandatory_.push_back(Constraint(key, value));
  }
  void AddOptional(const std::string& key, const std::string& value) {
    optional_.push_back(Constraint(key, value));
  }

  virtual const Constraints& GetMandatory() const  { return mandatory_; }
  virtual const Constraints& GetOptional() const { return optional_; }

 private:
  Constraints mandatory_;
  Constraints optional_;
};

class StateObserver : public ObserverInterface {
 public:
  explicit StateObserver(VideoSourceInterface* source)
     : state_(source->state()),
       source_(source) {
  }
  virtual void OnChanged() {
    state_ = source_->state();
  }
  MediaSourceInterface::SourceState state() const { return state_; }

 private:
  MediaSourceInterface::SourceState state_;
  talk_base::scoped_refptr<VideoSourceInterface> source_;
};

class LocalVideoSourceTest : public testing::Test {
 protected:
  LocalVideoSourceTest()
      : channel_manager_(new cricket::ChannelManager(
          new cricket::FakeMediaEngine(),
          new cricket::FakeDeviceManager(), talk_base::Thread::Current())) {
  }

  void SetUp() {
    ASSERT_TRUE(channel_manager_->Init());
    capturer_ = new TestVideoCapturer();
  }

  void CreateLocalVideoSource() {
    CreateLocalVideoSource(NULL);
  }

  void CreateLocalVideoSource(
      const webrtc::MediaConstraintsInterface* constraints) {
    // VideoSource take ownership of |capturer_|
    local_source_ = LocalVideoSource::Create(channel_manager_.get(),
                                             capturer_,
                                             constraints);

    ASSERT_TRUE(local_source_.get() != NULL);
    EXPECT_EQ(capturer_, local_source_->GetVideoCapturer());

    state_observer_.reset(new StateObserver(local_source_));
    local_source_->RegisterObserver(state_observer_.get());
    local_source_->AddSink(&renderer_);
  }

  TestVideoCapturer* capturer_;  // Raw pointer. Owned by local_source_.
  cricket::FakeVideoRenderer renderer_;
  talk_base::scoped_ptr<cricket::ChannelManager> channel_manager_;
  talk_base::scoped_ptr<StateObserver> state_observer_;
  talk_base::scoped_refptr<LocalVideoSource> local_source_;
};


// Test that a LocalVideoSource transition to kLive state when the capture
// device have started and kEnded if it is stopped.
// It also test that an output can receive video frames.
TEST_F(LocalVideoSourceTest, StartStop) {
  // Initialize without constraints.
  CreateLocalVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);

  ASSERT_TRUE(capturer_->CaptureFrame());
  EXPECT_EQ(1, renderer_.num_rendered_frames());

  capturer_->Stop();
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that a LocalVideoSource transition to kEnded if the capture device
// fails.
TEST_F(LocalVideoSourceTest, CameraFailed) {
  CreateLocalVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);

  capturer_->SignalStateChange(capturer_, cricket::CS_FAILED);
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that the capture output is CIF if we set max constraints to CIF.
// and the capture device support CIF.
TEST_F(LocalVideoSourceTest, MandatoryConstraintCif5Fps) {
  TestConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMaxWidth, "352");
  constraints.AddMandatory(MediaConstraintsInterface::kMaxHeight, "288");
  constraints.AddMandatory(MediaConstraintsInterface::kMaxFrameRate, "5");

  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format  = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(352 , format->width);
  EXPECT_EQ(288 , format->height);
  EXPECT_EQ(5 , format->framerate());
}

// Test that the capture output is 720P if the camera support it and the
// optional constraint is set to 720P.
TEST_F(LocalVideoSourceTest, MandatoryMinVgaOptional720P) {
  TestConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMinWidth, "640");
  constraints.AddMandatory(MediaConstraintsInterface::kMinHeight, "480");
  constraints.AddOptional(MediaConstraintsInterface::kMinWidth, "1280");
  constraints.AddOptional(MediaConstraintsInterface::kMinAspectRatio,
                          talk_base::ToString<double>(1280.0 / 720));

  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format  = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(1280 , format->width);
  EXPECT_EQ(720 , format->height);
  EXPECT_EQ(30 , format->framerate());
}

// Test that the capture output have aspect ratio 4:3 if a mandatory constraint
// require it even if an optional constraint request a higher resolution
// that don't have this aspect ratio.
TEST_F(LocalVideoSourceTest, MandatoryAspectRatio4To3) {
  TestConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMinWidth, "640");
  constraints.AddMandatory(MediaConstraintsInterface::kMinHeight, "480");
  constraints.AddMandatory(MediaConstraintsInterface::kMaxAspectRatio,
                           talk_base::ToString<double>(640.0 / 480));
  constraints.AddOptional(MediaConstraintsInterface::kMinWidth, "1280");

  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format  = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(640 , format->width);
  EXPECT_EQ(480 , format->height);
  EXPECT_EQ(30 , format->framerate());
}


// Test that the source state transition to kEnded if the mandatory aspect ratio
// is set higher than supported.
TEST_F(LocalVideoSourceTest, MandatoryAspectRatioTooHigh) {
  TestConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMinAspectRatio,
                           talk_base::ToString<int>(2));
  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that the source ignores an optional aspect ratio that is higher than
// supported.
TEST_F(LocalVideoSourceTest, OptionalAspectRatioTooHigh) {
  TestConstraints constraints;
  constraints.AddOptional(MediaConstraintsInterface::kMinAspectRatio,
                          talk_base::ToString<int>(2));
  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format  = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  double aspect_ratio = static_cast<double>(format->width) / format->height;
  EXPECT_LT(aspect_ratio, 2);
}

// Test that the source starts video with the default resolution if the
// camera doesn't support capability enumeration and there are no constraints.
TEST_F(LocalVideoSourceTest, NoCameraCapability) {
  capturer_->TestWithoutCameraFormats();

  CreateLocalVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format  = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(640 , format->width);
  EXPECT_EQ(480 , format->height);
  EXPECT_EQ(30 , format->framerate());
}

// Test that the source can start the video and get the requested aspect ratio
// if the camera doesn't support capability enumeration and the aspect ratio is
// set.
TEST_F(LocalVideoSourceTest, NoCameraCapability16To9Ratio) {
  capturer_->TestWithoutCameraFormats();

  TestConstraints constraints;
  double requested_aspect_ratio = 640.0 / 360;
  constraints.AddMandatory(MediaConstraintsInterface::kMinWidth, "640");
  constraints.AddMandatory(MediaConstraintsInterface::kMinAspectRatio,
                           talk_base::ToString<double>(requested_aspect_ratio));

  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format  = capturer_->GetCaptureFormat();
  double aspect_ratio = static_cast<double>(format->width) / format->height;
  EXPECT_LE(requested_aspect_ratio, aspect_ratio);
}

// Test that the source state transitions to kEnded if an unknown mandatory
// constraint is found.
TEST_F(LocalVideoSourceTest, InvalidMandatoryConstraint) {
  TestConstraints constraints;
  constraints.AddMandatory("weird key", "640");

  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that the source ignores an unknown optional constraint.
TEST_F(LocalVideoSourceTest, InvalidOptionalConstraint) {
  TestConstraints constraints;
  constraints.AddOptional("weird key", "640");

  CreateLocalVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
}
