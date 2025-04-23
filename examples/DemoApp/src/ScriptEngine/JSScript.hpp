#pragma once

#include <v8.h>

#include <memory>

class JSScript
{
  public:
    JSScript(std::shared_ptr<v8::Isolate> isolate, v8::Local<v8::ObjectTemplate> global,
             v8::Local<v8::String> sourceCode);

    ~JSScript() = default;

    bool HasFunction(const char* name);

    void RunFunction(const char* name, const std::vector<v8::Local<v8::Value>>& args = {});

  private:
    std::shared_ptr<v8::Isolate> _isolate;
    v8::Global<v8::Context> _context;

    v8::Local<v8::Function> GetFunction(const char* name);
};