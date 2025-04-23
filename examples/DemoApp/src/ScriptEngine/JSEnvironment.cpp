#include "JSEnvironment.hpp"

#include <fstream>
#include <iostream>

struct IsolateDeleter
{
    void operator()(v8::Isolate* isolate) const noexcept
    {
        if (isolate)
            isolate->Dispose();
    }
};

JSEnvironment::JSEnvironment() : _allocator(v8::ArrayBuffer::Allocator::NewDefaultAllocator()), _isolate(nullptr)
{
    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = _allocator.get();

    _isolate = std::shared_ptr<v8::Isolate>(v8::Isolate::New(params), IsolateDeleter{});

    v8::Isolate::Scope isolate_scope(_isolate.get());
    // create a stack-allocated handle scope
    // this is important for memory management in V8
    v8::HandleScope handle_scope(_isolate.get());

    // create a template for the global object where we set the
    // built-in global functions
    v8::Local<v8::ObjectTemplate> localTemplate = v8::ObjectTemplate::New(_isolate.get());
    _globalTemplate.Reset(_isolate.get(), localTemplate);

    // set the print function in the global template
    SetFunction("print", DefaultPrint);
}

void JSEnvironment::SetFunction(const char* name, v8::FunctionCallback fn)
{
    v8::Isolate::Scope isolate_scope(_isolate.get());
    v8::HandleScope handle_scope(_isolate.get());
    v8::Local<v8::ObjectTemplate> localTemplate = _globalTemplate.Get(_isolate.get());

    localTemplate->Set(_isolate.get(), name, v8::FunctionTemplate::New(_isolate.get(), fn));
}

JSScript JSEnvironment::CreateScript(const std::string& filePath)
{
    v8::Isolate::Scope isolate_scope(_isolate.get());
    v8::HandleScope handle_scope(_isolate.get());

    v8::Local<v8::String> sourceCode;

    if (!ReadFile(filePath).ToLocal(&sourceCode))
    {
        std::cerr << "Error reading '" << filePath << "'." << std::endl;
        return JSScript(nullptr, v8::Local<v8::ObjectTemplate>(), v8::Local<v8::String>());
    }

    v8::Local<v8::ObjectTemplate> localTemplate = _globalTemplate.Get(_isolate.get());

    return JSScript(_isolate, localTemplate, sourceCode);
}

void JSEnvironment::DefaultPrint(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    // if no arguments are passed, return
    if (info.Length() < 1)
        return;

    // create a handle scope for the current isolate
    v8::HandleScope handle_scope(info.GetIsolate());

    for (int i = 0; i < info.Length(); ++i)
    {
        if (i > 0)
            std::cout << ' ';
        v8::String::Utf8Value str(info.GetIsolate(), info[i]);
        std::cout << *str;
    }
    std::cout << std::endl;
}

v8::MaybeLocal<v8::String> JSEnvironment::ReadFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
        return v8::MaybeLocal<v8::String>();

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    v8::Local<v8::String> result;
    if (!v8::String::NewFromUtf8(_isolate.get(), contents.c_str(), v8::NewStringType::kNormal).ToLocal(&result))
        return v8::MaybeLocal<v8::String>();

    return v8::MaybeLocal<v8::String>(result);
}