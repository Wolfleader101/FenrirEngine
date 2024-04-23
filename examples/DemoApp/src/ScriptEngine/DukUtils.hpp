#pragma once

#include <duktape.h>

#include <fstream>
#include <sstream>
#include <string>

#include "FenrirLogger/ILogger.hpp"

#define DUK_STRING_PUSH_SAFE (1 << 0) /* no error if file does not exist */

namespace Fenrir
{

    // TODO fix logger later
    static Fenrir::ILogger* g_logger = nullptr;

    void fatal_callback(void* udata, const char* msg)
    {
        (void)udata; /* ignored in this case, silence warning */

        /* Note that 'msg' may be NULL. */
        std::string str = "*** FATAL SCRIPT ERROR: " + std::string((msg ? msg : "no message"));
        // TODO LOGGER
        if (g_logger)
        {
            g_logger->Fatal(str);
        }
        abort();
    }

    void duk_push_string_file_raw(duk_context* ctx, const std::string& path, duk_uint_t flags)
    {
        // Open the file
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            if (flags & DUK_STRING_PUSH_SAFE)
            {
                duk_push_undefined(ctx);
                return;
            }
            else
            {
                duk_error(ctx, DUK_ERR_ERROR, "Could not open file: %s", path.c_str());
                return; // Control never returns here
            }
        }

        // Read the file into a string
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string file_contents = ss.str();

        if (file.fail())
        {
            if (flags & DUK_STRING_PUSH_SAFE)
            {
                duk_push_undefined(ctx);
                return;
            }
            else
            {
                duk_error(ctx, DUK_ERR_ERROR, "Could not read file: %s", path.c_str());
                return; // Control never returns here
            }
        }

        // Push the file contents onto the Duktape stack as a string
        duk_push_lstring(ctx, file_contents.c_str(), file_contents.size());
    }

    duk_int_t duk_peval_file(duk_context* ctx, const char* path)
    {
        duk_int_t rc;

        duk_push_string_file_raw(ctx, path, DUK_STRING_PUSH_SAFE);
        duk_push_string(ctx, path);
        rc = duk_pcompile(ctx, DUK_COMPILE_EVAL);
        if (rc != 0)
        {
            return rc; // error
        }
        duk_push_global_object(ctx); /* 'this' binding */
        rc = duk_pcall_method(ctx, 0);
        return rc; // no error
    }
} // namespace Fenrir