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
    the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
    Boston, MA 02111-1307, USA.
*/

#include "config.h"
#include "V8TestOverloadedConstructors.h"

#include "BindingState.h"
#include "ContextFeatures.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "RuntimeEnabledFeatures.h"
#include "V8ArrayBuffer.h"
#include "V8ArrayBufferView.h"
#include "V8Binding.h"
#include "V8Blob.h"
#include "V8DOMWrapper.h"
#include <wtf/ArrayBuffer.h>
#include <wtf/ArrayBufferView.h>
#include <wtf/UnusedParam.h>

#if ENABLE(BINDING_INTEGRITY)
#if defined(OS_WIN)
#pragma warning(disable: 4483)
extern "C" { extern void (*const __identifier("??_7TestOverloadedConstructors@WebCore@@6B@")[])(); }
#else
extern "C" { extern void* _ZTVN7WebCore26TestOverloadedConstructorsE[]; }
#endif
#endif // ENABLE(BINDING_INTEGRITY)

namespace WebCore {

#if ENABLE(BINDING_INTEGRITY)
inline void checkTypeOrDieTrying(TestOverloadedConstructors* object)
{
    void* actualVTablePointer = *(reinterpret_cast<void**>(object));
#if defined(OS_WIN)
    void* expectedVTablePointer = reinterpret_cast<void*>(__identifier("??_7TestOverloadedConstructors@WebCore@@6B@"));
#else
    void* expectedVTablePointer = &_ZTVN7WebCore26TestOverloadedConstructorsE[2];
#endif
    if (actualVTablePointer != expectedVTablePointer)
        CRASH();
}
#endif // ENABLE(BINDING_INTEGRITY)

WrapperTypeInfo V8TestOverloadedConstructors::info = { V8TestOverloadedConstructors::GetTemplate, V8TestOverloadedConstructors::derefObject, 0, 0, 0, V8TestOverloadedConstructors::installPerContextPrototypeProperties, 0, WrapperTypeObjectPrototype };

namespace TestOverloadedConstructorsV8Internal {

template <typename T> void V8_USE(T) { }

} // namespace TestOverloadedConstructorsV8Internal

v8::Handle<v8::Value> V8TestOverloadedConstructors::constructor1Callback(const v8::Arguments& args)
{
    
    V8TRYCATCH(ArrayBuffer*, arrayBuffer, V8ArrayBuffer::HasInstance(MAYBE_MISSING_PARAMETER(args, 0, DefaultIsUndefined), args.GetIsolate()) ? V8ArrayBuffer::toNative(v8::Handle<v8::Object>::Cast(MAYBE_MISSING_PARAMETER(args, 0, DefaultIsUndefined))) : 0);

    RefPtr<TestOverloadedConstructors> impl = TestOverloadedConstructors::create(arrayBuffer);
    v8::Handle<v8::Object> wrapper = args.Holder();

    V8DOMWrapper::associateObjectWithWrapper(impl.release(), &info, wrapper, args.GetIsolate(), WrapperConfiguration::Dependent);
    return wrapper;
}

v8::Handle<v8::Value> V8TestOverloadedConstructors::constructor2Callback(const v8::Arguments& args)
{
    
    V8TRYCATCH(ArrayBufferView*, arrayBufferView, V8ArrayBufferView::HasInstance(MAYBE_MISSING_PARAMETER(args, 0, DefaultIsUndefined), args.GetIsolate()) ? V8ArrayBufferView::toNative(v8::Handle<v8::Object>::Cast(MAYBE_MISSING_PARAMETER(args, 0, DefaultIsUndefined))) : 0);

    RefPtr<TestOverloadedConstructors> impl = TestOverloadedConstructors::create(arrayBufferView);
    v8::Handle<v8::Object> wrapper = args.Holder();

    V8DOMWrapper::associateObjectWithWrapper(impl.release(), &info, wrapper, args.GetIsolate(), WrapperConfiguration::Dependent);
    return wrapper;
}

v8::Handle<v8::Value> V8TestOverloadedConstructors::constructor3Callback(const v8::Arguments& args)
{
    
    V8TRYCATCH(Blob*, blob, V8Blob::HasInstance(MAYBE_MISSING_PARAMETER(args, 0, DefaultIsUndefined), args.GetIsolate()) ? V8Blob::toNative(v8::Handle<v8::Object>::Cast(MAYBE_MISSING_PARAMETER(args, 0, DefaultIsUndefined))) : 0);

    RefPtr<TestOverloadedConstructors> impl = TestOverloadedConstructors::create(blob);
    v8::Handle<v8::Object> wrapper = args.Holder();

    V8DOMWrapper::associateObjectWithWrapper(impl.release(), &info, wrapper, args.GetIsolate(), WrapperConfiguration::Dependent);
    return wrapper;
}

v8::Handle<v8::Value> V8TestOverloadedConstructors::constructor4Callback(const v8::Arguments& args)
{
    
    V8TRYCATCH_FOR_V8STRINGRESOURCE(V8StringResource<>, string, MAYBE_MISSING_PARAMETER(args, 0, DefaultIsUndefined));

    RefPtr<TestOverloadedConstructors> impl = TestOverloadedConstructors::create(string);
    v8::Handle<v8::Object> wrapper = args.Holder();

    V8DOMWrapper::associateObjectWithWrapper(impl.release(), &info, wrapper, args.GetIsolate(), WrapperConfiguration::Dependent);
    return wrapper;
}

v8::Handle<v8::Value> V8TestOverloadedConstructors::constructorCallback(const v8::Arguments& args)
{
    if (!args.IsConstructCall())
        return throwTypeError("DOM object constructor cannot be called as a function.", args.GetIsolate());

    if (ConstructorMode::current() == ConstructorMode::WrapExistingObject)
        return args.Holder();
    if ((args.Length() == 1 && (V8ArrayBuffer::HasInstance(args[0], args.GetIsolate()))))
        return constructor1Callback(args);
    if ((args.Length() == 1 && (V8ArrayBufferView::HasInstance(args[0], args.GetIsolate()))))
        return constructor2Callback(args);
    if ((args.Length() == 1 && (V8Blob::HasInstance(args[0], args.GetIsolate()))))
        return constructor3Callback(args);
    if (args.Length() == 1)
        return constructor4Callback(args);
    if (args.Length() < 1)
        return throwNotEnoughArgumentsError(args.GetIsolate());
    return throwTypeError(0, args.GetIsolate());
}

static v8::Persistent<v8::FunctionTemplate> ConfigureV8TestOverloadedConstructorsTemplate(v8::Persistent<v8::FunctionTemplate> desc, v8::Isolate* isolate)
{
    desc->ReadOnlyPrototype();

    v8::Local<v8::Signature> defaultSignature;
    defaultSignature = V8DOMConfiguration::configureTemplate(desc, "TestOverloadedConstructors", v8::Persistent<v8::FunctionTemplate>(), V8TestOverloadedConstructors::internalFieldCount,
        0, 0,
        0, 0, isolate);
    UNUSED_PARAM(defaultSignature); // In some cases, it will not be used.
    desc->SetCallHandler(V8TestOverloadedConstructors::constructorCallback);
    

    // Custom toString template
    desc->Set(v8::String::NewSymbol("toString"), V8PerIsolateData::current()->toStringTemplate());
    return desc;
}

v8::Persistent<v8::FunctionTemplate> V8TestOverloadedConstructors::GetRawTemplate(v8::Isolate* isolate)
{
    V8PerIsolateData* data = V8PerIsolateData::from(isolate);
    V8PerIsolateData::TemplateMap::iterator result = data->rawTemplateMap().find(&info);
    if (result != data->rawTemplateMap().end())
        return result->value;

    v8::HandleScope handleScope;
    v8::Persistent<v8::FunctionTemplate> templ = createRawTemplate(isolate);
    data->rawTemplateMap().add(&info, templ);
    return templ;
}

v8::Persistent<v8::FunctionTemplate> V8TestOverloadedConstructors::GetTemplate(v8::Isolate* isolate)
{
    V8PerIsolateData* data = V8PerIsolateData::from(isolate);
    V8PerIsolateData::TemplateMap::iterator result = data->templateMap().find(&info);
    if (result != data->templateMap().end())
        return result->value;

    v8::HandleScope handleScope;
    v8::Persistent<v8::FunctionTemplate> templ =
        ConfigureV8TestOverloadedConstructorsTemplate(GetRawTemplate(isolate), isolate);
    data->templateMap().add(&info, templ);
    return templ;
}

bool V8TestOverloadedConstructors::HasInstance(v8::Handle<v8::Value> value, v8::Isolate* isolate)
{
    return GetRawTemplate(isolate)->HasInstance(value);
}


v8::Handle<v8::Object> V8TestOverloadedConstructors::createWrapper(PassRefPtr<TestOverloadedConstructors> impl, v8::Handle<v8::Object> creationContext, v8::Isolate* isolate)
{
    ASSERT(impl.get());
    ASSERT(DOMDataStore::getWrapper(impl.get(), isolate).IsEmpty());

#if ENABLE(BINDING_INTEGRITY)
    checkTypeOrDieTrying(impl.get());
#endif

    v8::Handle<v8::Object> wrapper = V8DOMWrapper::createWrapper(creationContext, &info, impl.get(), isolate);
    if (UNLIKELY(wrapper.IsEmpty()))
        return wrapper;

    installPerContextProperties(wrapper, impl.get(), isolate);
    V8DOMWrapper::associateObjectWithWrapper(impl, &info, wrapper, isolate, hasDependentLifetime ? WrapperConfiguration::Dependent : WrapperConfiguration::Independent);
    return wrapper;
}
void V8TestOverloadedConstructors::derefObject(void* object)
{
    static_cast<TestOverloadedConstructors*>(object)->deref();
}

} // namespace WebCore
