#include "ScriptEngine.hpp"

JSEngine::JSEngine()
{
    v8::V8::InitializeICUDefaultLocation(".");
    v8::V8::InitializeExternalStartupData(".");
    _platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(_platform.get());
    v8::V8::Initialize();
}
JSEngine::~JSEngine()
{
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
}
