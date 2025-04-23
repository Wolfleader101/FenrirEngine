#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <libplatform/libplatform.h>
#include <v8.h>

// TODO investigate if we can make this a singleton/static class
class JSEngine
{
  public:
    JSEngine() = default;
    ~JSEngine() = default;

    void Init()
    {
        v8::V8::InitializeICUDefaultLocation(".");
        v8::V8::InitializeExternalStartupData(".");
        _platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(_platform.get());
        v8::V8::Initialize();
    }

    void Shutdown()
    {
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
    }

  private:
    std::unique_ptr<v8::Platform> _platform;
};

struct IsolateDeleter
{
    void operator()(v8::Isolate* isolate) const noexcept
    {
        if (isolate)
            isolate->Dispose();
    }
};

class JSScript
{
  public:
    JSScript(std::shared_ptr<v8::Isolate> isolate, v8::Local<v8::ObjectTemplate> global,
             v8::Local<v8::String> sourceCode)
        : _isolate(isolate)
    {
        v8::Isolate::Scope isolate_scope(_isolate.get());
        v8::HandleScope handle_scope(_isolate.get());

        v8::Local<v8::Context> ctx = v8::Context::New(_isolate.get(), nullptr, global);
        _context.Reset(_isolate.get(), ctx);

        // create context_scope
        v8::Context::Scope context_scope(ctx);

        // try catch block to handle exceptions
        v8::TryCatch try_catch(_isolate.get());

        // compile the script and check for errors
        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(ctx, sourceCode).ToLocal(&script))
        {
            // If there was an error, print it and return.
            v8::String::Utf8Value error(_isolate.get(), try_catch.Exception());

            std::cerr << "Error compiling script: " << *error << std::endl;
            return;
        }

        // Run the script and check for errors
        v8::Local<v8::Value> result;
        if (!script->Run(ctx).ToLocal(&result))
        {
            // If there was an error, print it and return.
            v8::String::Utf8Value error(_isolate.get(), try_catch.Exception());
            std::cerr << "Error running script: " << *error << std::endl;
            return;
        }
    }

    ~JSScript() = default; // destructor

    bool HasFunction(const char* name)
    {
        v8::Isolate::Scope isolate_scope(_isolate.get());
        v8::HandleScope handle_scope(_isolate.get());

        v8::Local<v8::Context> ctx = _context.Get(_isolate.get());

        // create a context scope for the current context
        v8::Context::Scope context_scope(ctx);

        // check if the function exists in the global object
        v8::Local<v8::String> fnName = v8::String::NewFromUtf8(_isolate.get(), name).ToLocalChecked();
        v8::Local<v8::Value> fnValue;
        if (!ctx->Global()->Get(ctx, fnName).ToLocal(&fnValue))
            return false;

        return fnValue->IsFunction();
    }

    void RunInitFunction()
    {
        v8::Isolate::Scope isolate_scope(_isolate.get());
        v8::HandleScope handle_scope(_isolate.get());

        v8::Local<v8::Context> ctx = _context.Get(_isolate.get());

        // create a context scope for the current context
        v8::Context::Scope context_scope(ctx);

        // create try-catch block to handle exceptions
        v8::TryCatch try_catch(_isolate.get());

        // check if the init function exists and is a function
        if (!HasFunction("init"))
            return;

        // get the init function from the global object
        v8::Local<v8::Function> initFunction = GetFunction("init");
        if (initFunction.IsEmpty())
            return;

        // call the init function with no arguments
        v8::Local<v8::Value> argv[] = {};

        v8::Local<v8::Value> fn_result;
        if (!initFunction->Call(ctx, ctx->Global(), 0, argv).ToLocal(&fn_result))
        {
            // If there was an error, print it and return.
            v8::String::Utf8Value error(_isolate.get(), try_catch.Exception());
            std::cerr << "Error calling init function: " << *error << std::endl;
            return;
        }
    }

    void RunFunction(const char* name, const std::vector<v8::Local<v8::Value>>& args = {})
    {
        v8::Isolate::Scope isolate_scope(_isolate.get());
        v8::HandleScope handle_scope(_isolate.get());

        v8::Local<v8::Context> ctx = _context.Get(_isolate.get());

        // create a context scope for the current context
        v8::Context::Scope context_scope(ctx);

        // create try-catch block to handle exceptions
        v8::TryCatch try_catch(_isolate.get());

        // check if the function exists and is a function
        if (!HasFunction(name))
            return;

        // get the function from the global object
        v8::Local<v8::Function> fn = GetFunction(name);
        if (fn.IsEmpty())
            return;

        // call the function with the arguments
        v8::Local<v8::Value> fn_result;
        std::vector<v8::Local<v8::Value>> argv(args.begin(), args.end());

        if (!fn->Call(ctx, ctx->Global(), static_cast<int>(argv.size()), argv.data()).ToLocal(&fn_result))
        {
            // If there was an error, print it and return.
            v8::String::Utf8Value error(_isolate.get(), try_catch.Exception());
            std::cerr << "Error calling function: " << *error << std::endl;
            return;
        }
    }

  private:
    std::shared_ptr<v8::Isolate> _isolate;
    v8::Global<v8::Context> _context;

    v8::Local<v8::Function> GetFunction(const char* name)
    {
        v8::Isolate::Scope isolate_scope(_isolate.get());
        v8::EscapableHandleScope handle_scope(_isolate.get());

        v8::Local<v8::Context> ctx = _context.Get(_isolate.get());

        v8::Context::Scope context_scope(ctx);

        v8::Local<v8::String> functionName = v8::String::NewFromUtf8(_isolate.get(), name).ToLocalChecked();
        v8::Local<v8::Value> functionValue;
        if (!ctx->Global()->Get(ctx, functionName).ToLocal(&functionValue))
            return v8::Local<v8::Function>();

        if (!functionValue->IsFunction())
        {
            v8::String::Utf8Value error(_isolate.get(), functionValue);
            std::cerr << "Error: " << *error << " is not a function" << std::endl;
            return v8::Local<v8::Function>();
        }

        return handle_scope.Escape(functionValue.As<v8::Function>());
    }
};

/**
 * @brief A class that manages the V8 isolate and its resources.
 * It also stores the global context and provides methods to create and execute scripts.
 *
 */
class JSEnvironment
{
  public:
    JSEnvironment() : _allocator(v8::ArrayBuffer::Allocator::NewDefaultAllocator()), _isolate(nullptr)
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

    ~JSEnvironment() = default;

    void SetFunction(const char* name, v8::FunctionCallback fn)
    {
        v8::Isolate::Scope isolate_scope(_isolate.get());
        v8::HandleScope handle_scope(_isolate.get());
        v8::Local<v8::ObjectTemplate> localTemplate = _globalTemplate.Get(_isolate.get());

        localTemplate->Set(_isolate.get(), name, v8::FunctionTemplate::New(_isolate.get(), fn));
    }

    JSScript CreateScript(const std::string& filePath)
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

  private:
    std::unique_ptr<v8::ArrayBuffer::Allocator> _allocator;
    std::shared_ptr<v8::Isolate> _isolate;
    v8::Global<v8::ObjectTemplate> _globalTemplate;

    static void DefaultPrint(const v8::FunctionCallbackInfo<v8::Value>& info)
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

    v8::MaybeLocal<v8::String> ReadFile(const std::string& filePath)
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
};
