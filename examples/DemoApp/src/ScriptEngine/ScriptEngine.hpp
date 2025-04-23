#pragma once

#include <libplatform/libplatform.h>
#include <v8.h>

#include <memory>

#include "JSEnvironment.hpp"
#include "JSScript.hpp"

// TODO investigate if we can make this a singleton/static class
class JSEngine
{
  public:
    JSEngine();
    ~JSEngine();

  private:
    std::unique_ptr<v8::Platform> _platform;
};
