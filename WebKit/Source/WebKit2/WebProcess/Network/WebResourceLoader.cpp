/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebResourceLoader.h"

#if ENABLE(NETWORK_PROCESS)

#include "DataReference.h"
#include "Logging.h"
#include "NetworkProcessConnection.h"
#include "PlatformCertificateInfo.h"
#include "WebCoreArgumentCoders.h"
#include "WebErrors.h"
#include "WebProcess.h"
#include <WebCore/ResourceError.h>
#include <WebCore/ResourceLoader.h>

using namespace WebCore;

namespace WebKit {

PassRefPtr<WebResourceLoader> WebResourceLoader::create(PassRefPtr<ResourceLoader> coreLoader)
{
    return adoptRef(new WebResourceLoader(coreLoader));
}

WebResourceLoader::WebResourceLoader(PassRefPtr<WebCore::ResourceLoader> coreLoader)
    : m_coreLoader(coreLoader)
{
}

WebResourceLoader::~WebResourceLoader()
{
}

CoreIPC::Connection* WebResourceLoader::connection() const
{
    return WebProcess::shared().networkConnection()->connection();
}

uint64_t WebResourceLoader::destinationID() const
{
    return m_coreLoader->identifier();
}

void WebResourceLoader::cancelResourceLoader()
{
    m_coreLoader->cancel();
}

void WebResourceLoader::willSendRequest(const ResourceRequest& proposedRequest, const ResourceResponse& redirectResponse, ResourceRequest& newRequest)
{
    LOG(Network, "(WebProcess) WebResourceLoader::willSendRequest to '%s'", proposedRequest.url().string().utf8().data());
    
    newRequest = proposedRequest;
    m_coreLoader->willSendRequest(newRequest, redirectResponse);
}

void WebResourceLoader::didReceiveResponseWithCertificateInfo(const ResourceResponse& response, const PlatformCertificateInfo& certificateInfo)
{
    LOG(Network, "(WebProcess) WebResourceLoader::didReceiveResponseWithCertificateInfo for '%s'. Status %d.", m_coreLoader->url().string().utf8().data(), response.httpStatusCode());
    ResourceResponse responseCopy(response);
    responseCopy.setCertificateChain(certificateInfo.certificateChain());
    m_coreLoader->didReceiveResponse(responseCopy);
}

void WebResourceLoader::didReceiveData(const CoreIPC::DataReference& data, int64_t encodedDataLength, bool allAtOnce)
{
    LOG(Network, "(WebProcess) WebResourceLoader::didReceiveData of size %i for '%s'", (int)data.size(), m_coreLoader->url().string().utf8().data());
    m_coreLoader->didReceiveData(reinterpret_cast<const char*>(data.data()), data.size(), encodedDataLength, allAtOnce);
}

void WebResourceLoader::didFinishResourceLoad(double finishTime)
{
    LOG(Network, "(WebProcess) WebResourceLoader::didFinishResourceLoad for '%s'", m_coreLoader->url().string().utf8().data());
    m_coreLoader->didFinishLoading(finishTime);
}

void WebResourceLoader::didFailResourceLoad(const ResourceError& error)
{
    LOG(Network, "(WebProcess) WebResourceLoader::didFailResourceLoad for '%s'", m_coreLoader->url().string().utf8().data());
    
    m_coreLoader->didFail(error);
}

void WebResourceLoader::didReceiveResource(const ShareableResource::Handle& handle, double finishTime)
{
    LOG(Network, "(WebProcess) WebResourceLoader::didReceiveResource for '%s'", m_coreLoader->url().string().utf8().data());

    RefPtr<ShareableResource> resource = ShareableResource::create(handle);

    // Only send data to the didReceiveData callback if it exists.
    if (!resource->size()) {
        // FIXME (NetworkProcess): Give ResourceLoader the ability to take ResourceBuffer arguments.
        // That will allow us to pass it along to CachedResources and allow them to hang on to the shared memory behind the scenes.
        // FIXME (NetworkProcess): Pass along the correct value for encodedDataLength.
        m_coreLoader->didReceiveData(reinterpret_cast<const char*>(resource->data()), resource->size(), -1 /* encodedDataLength */ , true);
    }

    m_coreLoader->didFinishLoading(finishTime);
}

void WebResourceLoader::canAuthenticateAgainstProtectionSpace(const ProtectionSpace& protectionSpace, bool& result)
{
    result = m_coreLoader->canAuthenticateAgainstProtectionSpace(protectionSpace);
}

} // namespace WebKit

#endif // ENABLE(NETWORK_PROCESS)
