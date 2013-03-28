/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "WebKitDOMFloat64Array.h"

#include "DOMObjectCache.h"
#include "ExceptionCode.h"
#include "JSMainThreadExecState.h"
#include "WebKitDOMBinding.h"
#include "WebKitDOMFloat32ArrayPrivate.h"
#include "WebKitDOMFloat64ArrayPrivate.h"
#include "WebKitDOMInt32ArrayPrivate.h"
#include "gobject/ConvertToUTF8String.h"
#include <wtf/GetPtr.h>
#include <wtf/RefPtr.h>

namespace WebKit {

WebKitDOMFloat64Array* kit(WebCore::Float64Array* obj)
{
    if (!obj)
        return 0;

    if (gpointer ret = DOMObjectCache::get(obj))
        return WEBKIT_DOM_FLOAT64ARRAY(ret);

    return wrapFloat64Array(obj);
}

WebCore::Float64Array* core(WebKitDOMFloat64Array* request)
{
    return request ? static_cast<WebCore::Float64Array*>(WEBKIT_DOM_OBJECT(request)->coreObject) : 0;
}

WebKitDOMFloat64Array* wrapFloat64Array(WebCore::Float64Array* coreObject)
{
    ASSERT(coreObject);
    return WEBKIT_DOM_FLOAT64ARRAY(g_object_new(WEBKIT_TYPE_DOM_FLOAT64ARRAY, "core-object", coreObject, NULL));
}

} // namespace WebKit

G_DEFINE_TYPE(WebKitDOMFloat64Array, webkit_dom_float64array, WEBKIT_TYPE_DOM_ARRAY_BUFFER_VIEW)

static void webkit_dom_float64array_class_init(WebKitDOMFloat64ArrayClass* requestClass)
{
}

static void webkit_dom_float64array_init(WebKitDOMFloat64Array* request)
{
}

WebKitDOMInt32Array*
webkit_dom_float64array_foo(WebKitDOMFloat64Array* self, WebKitDOMFloat32Array* array)
{
    WebCore::JSMainThreadNullState state;
    g_return_val_if_fail(WEBKIT_DOM_IS_FLOAT64ARRAY(self), 0);
    g_return_val_if_fail(WEBKIT_DOM_IS_FLOAT32ARRAY(array), 0);
    WebCore::Float64Array* item = WebKit::core(self);
    WebCore::Float32Array* convertedArray = WebKit::core(array);
    RefPtr<WebCore::Int32Array> gobjectResult = WTF::getPtr(item->foo(convertedArray));
    return WebKit::kit(gobjectResult.get());
}

