#pragma once

#include <duktape.h>

namespace Fenrir
{
    class ILogger;

    static duk_ret_t native_print(duk_context* ctx)
    {
        duk_push_current_function(ctx);
        duk_get_prop_string(ctx, -1, "_ILogger_this");
        ILogger* logger = static_cast<ILogger*>(duk_to_pointer(ctx, -1));
        duk_pop_2(ctx); // pop off the function and logger pointer

        const char* message = duk_safe_to_string(ctx, 0); // assume the message is the first argument
        logger->Log(message);
        return 0;
    }

    static duk_ret_t native_print_info(duk_context* ctx)
    {
        duk_push_current_function(ctx);
        duk_get_prop_string(ctx, -1, "_ILogger_this");
        ILogger* logger = static_cast<ILogger*>(duk_to_pointer(ctx, -1));
        duk_pop_2(ctx); // pop off the function and logger pointer

        const char* message = duk_safe_to_string(ctx, 0); // assume the message is the first argument
        logger->Info(message);
        return 0;
    }

    static duk_ret_t native_print_warn(duk_context* ctx)
    {
        duk_push_current_function(ctx);
        duk_get_prop_string(ctx, -1, "_ILogger_this");
        ILogger* logger = static_cast<ILogger*>(duk_to_pointer(ctx, -1));
        duk_pop_2(ctx); // pop off the function and logger pointer

        const char* message = duk_safe_to_string(ctx, 0); // assume the message is the first argument
        logger->Warn(message);
        return 0;
    }

    static duk_ret_t native_print_err(duk_context* ctx)
    {
        duk_push_current_function(ctx);
        duk_get_prop_string(ctx, -1, "_ILogger_this");
        ILogger* logger = static_cast<ILogger*>(duk_to_pointer(ctx, -1));
        duk_pop_2(ctx); // pop off the function and logger pointer

        const char* message = duk_safe_to_string(ctx, 0); // assume the message is the first argument
        logger->Error(message);
        return 0;
    }

    static duk_ret_t native_print_fatal(duk_context* ctx)
    {
        duk_push_current_function(ctx);
        duk_get_prop_string(ctx, -1, "_ILogger_this");
        ILogger* logger = static_cast<ILogger*>(duk_to_pointer(ctx, -1));
        duk_pop_2(ctx); // pop off the function and logger pointer

        const char* message = duk_safe_to_string(ctx, 0); // assume the message is the first argument
        logger->Fatal(message);
        return 0;
    }

} // namespace Fenrir