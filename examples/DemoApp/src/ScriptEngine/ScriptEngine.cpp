#include "ScriptEngine.hpp"

#include "DukUtils.hpp"

#include "NativeFunctions.hpp"
#include "dukglue/dukglue.h"

namespace Fenrir
{
    ScriptEngine::ScriptEngine(Fenrir::ILogger& logger, const Time& time) : m_heap(), m_logger(logger), m_time(time)
    {
        // TODO FIX THIS LATER
        g_logger = &logger;

        this->RegisterFunctions(m_heap.GetContext());
        // TODO - register global vars
        this->RegisterVars(m_heap.GetContext());

        CreateScript("assets/scripts/test.js");

        DukTie::DukFunction<void, int> func(m_heap.GetContext(), "testFunc");

        auto fn = [](int a) { g_logger->Fatal("Function called with {0}", a); };

        func.Set(fn);

        func.Call(10);
        // CreateScript("assets/scripts/test1.js");
    }

    JSScript ScriptEngine::CreateScript(const std::string& scriptPath)
    {
        JSScript script(m_heap, scriptPath);

        if (!script.env.IsValid())
        {
            m_logger.Fatal("Failed to create Duktape context");
            return script;
        }

        // TODO make context have run file function
        //! run using the scripts own context, (you probably dont want to run every script on creation tho?)
        this->RunFile(script.env.GetContext(), scriptPath);

        // Store the new context so that we can destroy it later
        //! this might be doubling handling, may not need to store a list of contexts

        m_scripts.push_back(script); //! just for testing - remove later

        return script;
    }

    void ScriptEngine::Update(Fenrir::App& app)
    {
        for (JSScript& script : m_scripts)
        {
            RunFunction(script.env.GetContext(), "update", {});
        }
    }

    JSType ScriptEngine::GetGlobal(const std::string& name)
    {
        return GetVariable(m_heap.GetContext(), name);
    }

    void ScriptEngine::SetGlobal(const std::string& name, const JSType& value)
    {
        SetVariable(m_heap.GetContext(), name, value);
    }

    JSType ScriptEngine::GetScriptVariable(const JSScript& script, const std::string& varName)
    {
        return GetVariable(script.env.GetContext(), varName);
    }

    void ScriptEngine::SetScriptVariable(const JSScript& script, const std::string& name, const JSType& value)
    {
        SetVariable(script.env.GetContext(), name, value);
    }

    void ScriptEngine::RunFunction(duk_context* ctx, const std::string& funcName, const std::vector<JSType>& args)
    {
        // Get the function
        duk_get_global_string(ctx, funcName.c_str());

        // Check if it's callable
        if (!duk_is_callable(ctx, -1))
        {

            m_logger.Fatal("Function {0} is not callable.", funcName);
            duk_pop(ctx); // Pop non-callable value from the stack
            return;
        }

        // Push arguments onto the stack
        for (const auto& arg : args)
        {
            PushArg(ctx, arg);
        }

        // Call the function with n arguments, and check for errors
        if (duk_pcall(ctx, static_cast<duk_idx_t>(args.size())) != DUK_EXEC_SUCCESS)
        {
            m_logger.Fatal("Error calling function: {0}", duk_safe_to_string(ctx, -1));
        }
        //! TODO pass in return type and return it here if it returns anything
        // else {
        //     VK_INFO("Function returned: {0}", duk_safe_to_string(ctx, -1));
        // }

        // Pop the result or error from the stack
        duk_pop(ctx);
    }

    void ScriptEngine::RunFile(duk_context* ctx, const std::string& file)
    {
        if (duk_peval_file(ctx, file.c_str()) != 0)
        {
            m_logger.Fatal("Could not evaluate file ({0}): {1}", file, duk_safe_to_string(ctx, -1));
            duk_pop(ctx);
            return;
        }
        // Clear the result of the script evaluation
        duk_pop(ctx);
    }

    JSType ScriptEngine::GetVariable(duk_context* ctx, const std::string& varName)
    {
        // Get the variable
        duk_get_global_string(ctx, varName.c_str());

        // Check the type and get the value
        if (duk_is_string(ctx, -1))
        {
            std::string val = duk_get_string(ctx, -1);
            duk_pop(ctx);
            return val;
        }
        else if (duk_is_number(ctx, -1))
        {
            double val = duk_get_number(ctx, -1);
            duk_pop(ctx);
            return val;
        }
        else if (duk_is_boolean(ctx, -1))
        {
            bool val = duk_get_boolean(ctx, -1);
            duk_pop(ctx);
            return val;
        }
        else if (duk_is_null_or_undefined(ctx, -1))
        {
            duk_pop(ctx);
            return std::string(); // Return empty string as null
        }
        else if (duk_is_object(ctx, -1))
        {
            auto obj = std::make_shared<JSObject>();
            duk_enum(ctx, -1, 0); /* Enumerate keys in object on top of stack */
            while (duk_next(ctx, -1, 1 /*get_value*/))
            {
                /* -1 is value, -2 is key */
                std::string key = duk_get_string(ctx, -2);
                // Recursively call GetVariable for object's properties.
                JSType val = GetVariable(ctx, key);

                obj->emplace(key, val);
                duk_pop_2(ctx); /* pop key value pair */
            }
            duk_pop(ctx); /* pop enumerator object */
            return obj;
        }
        else
        {
            // Unsupported type or an error occurred
            m_logger.Fatal("Unsupported variable type or error occurred while getting variable {0}", varName);
            duk_pop(ctx);
            return std::string(); // Return empty string for error
        }
    }

    void ScriptEngine::SetVariable(duk_context* ctx, const std::string& varName, const JSType& value)
    {
        // Push variable name onto the stack
        duk_push_string(ctx, varName.c_str());

        // Push value onto the stack
        PushArg(ctx, value);

        // Set the variable in the global object
        duk_put_global_string(ctx, varName.c_str());
    }

    void ScriptEngine::PushArg(duk_context* ctx, const JSType& arg)
    {
        if (auto pval = std::get_if<std::shared_ptr<JSObject>>(&arg))
        {
            duk_push_object(ctx);
            auto& map = **pval;
            for (const auto& [key, value] : map)
            {
                PushArg(ctx, value);
                duk_put_prop_string(ctx, -2, key.c_str());
            }
        }
        else
        {
            std::visit(
                [this, ctx](auto&& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, int>)
                        duk_push_int(ctx, val);
                    else if constexpr (std::is_same_v<T, double>)
                        duk_push_number(ctx, val);
                    else if constexpr (std::is_same_v<T, bool>)
                        duk_push_boolean(ctx, val);
                    else if constexpr (std::is_same_v<T, std::string>)
                        duk_push_string(ctx, val.c_str());
                },
                arg);
        }
    }

    void ScriptEngine::RegisterFunctions(duk_context* ctx)
    {
        //! TODO MOVE THESE INTO SEPERATE FILE
        duk_push_c_function(ctx, native_print, 1 /* single argument expected */);
        duk_push_pointer(ctx, static_cast<void*>(&m_logger));
        duk_put_prop_string(ctx, -2, "_ILogger_this");
        duk_put_global_string(ctx, "print");

        duk_push_c_function(ctx, native_print_info, 1);
        duk_push_pointer(ctx, static_cast<void*>(&m_logger));
        duk_put_prop_string(ctx, -2, "_ILogger_this");
        duk_put_global_string(ctx, "print_info");

        // Repeat for warn, error, critical
        duk_push_c_function(ctx, native_print_warn, 1);
        duk_push_pointer(ctx, static_cast<void*>(&m_logger));
        duk_put_prop_string(ctx, -2, "_ILogger_this");
        duk_put_global_string(ctx, "print_warn");

        duk_push_c_function(ctx, native_print_err, 1);
        duk_push_pointer(ctx, static_cast<void*>(&m_logger));
        duk_put_prop_string(ctx, -2, "_ILogger_this");
        duk_put_global_string(ctx, "print_err");

        duk_push_c_function(ctx, native_print_fatal, 1);
        duk_push_pointer(ctx, static_cast<void*>(&m_logger));
        duk_put_prop_string(ctx, -2, "_ILogger_this");
        duk_put_global_string(ctx, "print_crit");
    }

    void ScriptEngine::RegisterVars(duk_context* ctx)
    {
        struct MyTime
        {
          public:
            MyTime() : m_time(nullptr)
            {
            }

            MyTime(Time* time) : m_time(time)
            {
            }

            double deltaTime() const
            {
                if (m_time != nullptr)
                    return m_time->deltaTime;
                else
                    return 0;
            }

          private:
            Time* m_time;
        };

        // TODO figure out where to put this so there isnt a mmeory leak
        MyTime* time = new MyTime(const_cast<Time*>(&m_time));

        // dukglue_register_constructor<MyTime>(ctx, "Time");

        dukglue_register_property(ctx, &MyTime::deltaTime, nullptr, "deltaTime");

        SetGlobal("Time", time);
    }

} // namespace Fenrir