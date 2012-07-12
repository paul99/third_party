/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "cc/CCTimer.h"
#include "TraceEvent.h"

namespace WebCore {

class CCTimerTask : public CCThread::Task {
public:
    explicit CCTimerTask(CCTimer* timer)
        : CCThread::Task(0)
        , m_timer(timer)
    {
    }

    ~CCTimerTask()
    {
        // FIXME: upstream this fix, wkb.ug/86513
    }

    void performTask()
    {
        if (!m_timer || !m_timer->m_client) {
            TRACE_EVENT("TimerExpired", 0, 0);
            return;
        }

        // FIXME: upstream this fix, wkb.ug/86513
        m_timer->m_task = 0;
        m_timer->m_client->onTimerFired();
    }

private:
    friend class CCTimer;

    CCTimer* m_timer; // null if cancelled
};

CCTimer::CCTimer(CCThread* thread, CCTimerClient* client)
    : m_client(client)
    , m_thread(thread)
    , m_task(0)
{
}

CCTimer::~CCTimer()
{
    stop();
}

void CCTimer::startOneShot(double intervalSeconds)
{
    stop();

    m_task = new CCTimerTask(this);

    // The thread expects delays in milliseconds.
    m_thread->postDelayedTask(adoptPtr(m_task), intervalSeconds * 1000.0);
}

void CCTimer::stop()
{
    if (!m_task)
        return;

    m_task->m_timer = 0;
    m_task = 0;
}

} // namespace WebCore
