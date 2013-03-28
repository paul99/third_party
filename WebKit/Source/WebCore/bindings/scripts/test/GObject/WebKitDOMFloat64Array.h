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

#ifndef WebKitDOMFloat64Array_h
#define WebKitDOMFloat64Array_h

#include <glib-object.h>
#include <webkitdom/WebKitDOMArrayBufferView.h>
#include <webkitdom/webkitdomdefines.h>

G_BEGIN_DECLS

#define WEBKIT_TYPE_DOM_FLOAT64ARRAY            (webkit_dom_float64array_get_type())
#define WEBKIT_DOM_FLOAT64ARRAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), WEBKIT_TYPE_DOM_FLOAT64ARRAY, WebKitDOMFloat64Array))
#define WEBKIT_DOM_FLOAT64ARRAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  WEBKIT_TYPE_DOM_FLOAT64ARRAY, WebKitDOMFloat64ArrayClass)
#define WEBKIT_DOM_IS_FLOAT64ARRAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), WEBKIT_TYPE_DOM_FLOAT64ARRAY))
#define WEBKIT_DOM_IS_FLOAT64ARRAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  WEBKIT_TYPE_DOM_FLOAT64ARRAY))
#define WEBKIT_DOM_FLOAT64ARRAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  WEBKIT_TYPE_DOM_FLOAT64ARRAY, WebKitDOMFloat64ArrayClass))

struct _WebKitDOMFloat64Array {
    WebKitDOMArrayBufferView parent_instance;
};

struct _WebKitDOMFloat64ArrayClass {
    WebKitDOMArrayBufferViewClass parent_class;
};

WEBKIT_API GType
webkit_dom_float64array_get_type (void);

/**
 * webkit_dom_float64array_foo:
 * @self: A #WebKitDOMFloat64Array
 * @array: A #WebKitDOMFloat32Array
 *
 * Returns: (transfer none):
 *
**/
WEBKIT_API WebKitDOMInt32Array*
webkit_dom_float64array_foo(WebKitDOMFloat64Array* self, WebKitDOMFloat32Array* array);

G_END_DECLS

#endif /* WebKitDOMFloat64Array_h */
