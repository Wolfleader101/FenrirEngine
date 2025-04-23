#include "JSScript.hpp"

#include <iostream>

JSScript::JSScript(std::shared_ptr<v8::Isolate> isolate, v8::Local<v8::ObjectTemplate> global,
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

bool JSScript::HasFunction(const char* name)
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

void JSScript::RunFunction(const char* name, const std::vector<v8::Local<v8::Value>>& args)
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

v8::Local<v8::Function> JSScript::GetFunction(const char* name)
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