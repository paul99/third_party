/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "cc/CCFrameRateController.h"

#include "TraceEvent.h"
#include "cc/CCProxy.h"
#include "cc/CCThread.h"
#include "cc/CCThreadTask.h"

namespace WebCore {

class CCFrameRateControllerTimeSourceAdapter : public CCTimeSourceClient {
public:
    static PassOwnPtr<CCFrameRateControllerTimeSourceAdapter> create(CCFrameRateController* frameRateController)
    {
        return adoptPtr(new CCFrameRateControllerTimeSourceAdapter(frameRateController));
    }
    virtual ~CCFrameRateControllerTimeSourceAdapter() { }

    virtual void onTimerTick() OVERRIDE { m_frameRateController->onTimerTick(); }
private:
    explicit CCFrameRateControllerTimeSourceAdapter(CCFrameRateController* frameRateController)
            : m_frameRateController(frameRateController) { }

    CCFrameRateController* m_frameRateController;
};

CCFrameRateController::CCFrameRateController(PassRefPtr<CCDelayBasedTimeSource> timer)
    : m_client(0)
    , m_numFramesPending(0)
    , m_timeSource(timer)
    , m_lastDroppedFrameTime(0)
{
    m_timeSourceClientAdapter = CCFrameRateControllerTimeSourceAdapter::create(this);
    m_timeSource->setClient(m_timeSourceClientAdapter.get());
}

CCFrameRateController::~CCFrameRateController()
{
    m_timeSource->setActive(false);
}

void CCFrameRateController::setActive(bool active)
{
    if (active == m_timeSource->active())
        return;

    if (active) {
        TRACE_EVENT("CCFrameRateController::setActive", 0, 0);
    } else {
        TRACE_EVENT("CCFrameRateController::setInactive", 0, 0);
    }
    m_timeSource->setActive(active);
}

void CCFrameRateController::setMaxFramesPending(int maxFramesPending)
{
    // On Android, we don't limit pending frames here, but we only kick off a new frame when
    // the previous frame finished (was dequeued), which usually gives us one frame pending
    // (with some exceptions such as forced draws).
}

void CCFrameRateController::onTimerTick()
{
    TRACE_EVENT("CCFrameRateController::onTimerFired", 0, 0);
    if (!m_client)
        return;

    if (m_numFramesPending >= 2) {
        TRACE_EVENT("CCFrameRateController::onTimerTickButMaxFramesPending", 0, 0);
        m_lastDroppedFrameTime = monotonicallyIncreasingTime();
        return;
    }

    if (m_client)
        m_client->beginFrame();
}

void CCFrameRateController::didVSync(double frameBeginMonotonic, double currentFrameIntervalInSec)
{
#if 0
    char buf[128];
    sprintf(buf, "now=%f,frameBeginMonotonic=%f,interval=%f", monotonicallyIncreasingTime(), frameBeginMonotonic, currentFrameIntervalInSec);
    TRACE_EVENT("CCFrameRateController::didVSync", 0, buf);
#else
    TRACE_EVENT("CCFrameRateController::didVSync", 0, 0);
#endif
    m_timeSource->setTimebaseAndInterval(frameBeginMonotonic, currentFrameIntervalInSec);
}

void CCFrameRateController::didBeginFrame()
{
    TRACE_EVENT("CCFrameRateController::didBeginFrame", 0, 0)
    m_numFramesPending++;
}

void CCFrameRateController::didFinishFrame()
{
    TRACE_EVENT("CCFrameRateController::didFinishFrame", 0, 0)
    m_numFramesPending--;
    if (m_numFramesPending == 1) {
        // When we are in 2-frames-deep mode, we will sometimes get a tick and
        // then just a few milliseconds later get a swapack. When this happens, the tick
        // is discarded because we have 2 frames pending. This logic detects the case of a
        // just-slightly late swapAck and issues the frame anyway. The rationale is that
        // the precise time that we draw isn't important, just that we do draw.
        double now = monotonicallyIncreasingTime();
        double timeSinceLastDroppedFrame = now - m_lastDroppedFrameTime;
        if (timeSinceLastDroppedFrame < 0.008) {
            m_lastDroppedFrameTime = 0; // Reset so we don't doubletick.
            TRACE_EVENT("reTickBecauseDropWasCorrected", 0, 0)
            onTimerTick();
            return;
        }
    }
}

void CCFrameRateController::didAbortAllPendingFrames()
{
    m_numFramesPending = 0;
}

}
