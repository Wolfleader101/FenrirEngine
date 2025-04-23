#pragma once

#include <memory>
#include <string>
#include <v8.h>

#include "JSScript.hpp"

/**
 * @brief A class that manages the V8 isolate and its resources.
 * It also stores the global context and provides methods to create and execute scripts.
 *
 */
class JSEnvironment
{
  public:
    JSEnvironment();
    ~JSEnvironment() = default;

    void SetFunction(const char* name, v8::FunctionCallback fn);

    JSScript CreateScript(const std::string& filePath);

  private:
    std::unique_ptr<v8::ArrayBuffer::Allocator> _allocator;
    std::shared_ptr<v8::Isolate> _isolate;
    v8::Global<v8::ObjectTemplate> _globalTemplate;

    static void DefaultPrint(const v8::FunctionCallbackInfo<v8::Value>& info);

    v8::MaybeLocal<v8::String> ReadFile(const std::string& filePath);
};
