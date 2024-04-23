#pragma once

#include <duktape.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirTime/Time.hpp"

namespace DukTie
{
    template <typename T>
    class DukType
    {
      public:
        /**
         * @brief Construct a new Duk Type object
         *
         * @param ctx duk context to use
         * @param typeName of the object
         */
        DukType(duk_context* ctx, const std::string& typeName) : m_ctx(ctx), m_typeName(typeName)
        {
            // dukglue_register_constructor<T>(m_ctx, m_typeName.c_str());
        }

        /**
         * @brief Set the Constructor object
         *
         * @tparam Args arguments to the constructor
         */
        template <typename... Args>
        void SetConstructor()
        {
            dukglue_register_constructor<T, Args...>(m_ctx, m_typeName.c_str());
        }

        /**
         * @brief Set the Method object
         *
         * @tparam RetType the return type of the method
         * @tparam Ts the arguments of the method
         * @param methodName the name of the method
         * @param methodPtr the pointer to the method
         */
        template <typename RetType, typename... Ts>
        void SetMethod(const std::string& methodName, RetType (T::*methodPtr)(Ts...))
        {
            dukglue_register_method(m_ctx, &T::methodName, methodName.c_str());
        }

        /**
         * @brief Set the Method object
         *
         * @tparam RetType the return type of the method
         * @tparam Ts the arguments of the method
         * @param methodName the name of the method
         * @param methodPtr the pointer to the method
         */
        template <typename RetType, typename... Ts>
        void SetMethod(const std::string& methodName, RetType (T::*methodPtr)(Ts...) const)
        {
            dukglue_register_method(m_ctx, &T::methodName, methodName.c_str());
        }

      private:
        duk_context* m_ctx;
        std::string m_typeName;
    };

    class DukContext
    {
      public:
        template <typename RetType, typename... Ts>
        void SetFunction(const std::string& funcName, RetType (*funcToCall)(Ts...))
        {
            dukglue_register_function(m_ctx.get(), funcToCall, funcName.c_str());
        }

        template <typename T>
        DukType<T> CreateType(const std::string& typeName)
        {
            return DukType<T>(m_ctx.get(), typeName);
        }

        template <typename T>
        void SetVar(const std::string& varName, T* value)
        {
            // Push the object to Duktape stack
            dukglue_push(m_ctx.get(), value);

            // Stash the object in the global object
            duk_put_global_string(m_ctx.get(), varName.c_str());
        }

        /**
         * @brief Get the underlying Duktape context
         * @deprecated this is deprecated and should not be used and will be removed in future. It currently is only
         * used for backward compatibility
         *
         * @return duk_context* the underlying Duktape context
         */
        [[deprecated("This function is deprecated.")]]
        duk_context* GetContext() const
        {
            return m_ctx.get();
        }

        /**
         * @brief Check if the Duk Context is valid
         *
         * @return true if the context is valid
         * @return false if the context is invalid
         */
        bool IsValid()
        {
            return m_ctx != nullptr;
        }

        bool operator==(const DukContext& other) const
        {
            return m_ctx == other.m_ctx;
        }

        bool operator!=(const DukContext& other) const
        {
            return m_ctx != other.m_ctx;
        }

        operator bool() const
        {
            return m_ctx != nullptr;
        }

      protected:
        std::shared_ptr<duk_context> m_ctx;
    };
    class DukState : public DukContext
    {
      public:
        /**
         * @brief Construct a new Duk Context object by creating a new heap
         *
         * It creates a new heap and pushes context onto it
         */
        DukState()
        {
            m_ctx = std::shared_ptr<duk_context>(duk_create_heap(nullptr, nullptr, nullptr, nullptr, nullptr),
                                                 [](duk_context* ctx) {
                                                     if (ctx)
                                                         duk_destroy_heap(ctx);
                                                 });
            // TODO error handling & fatal callback
            // if (!m_ctx)
            // {
            //     m_logger.Fatal("Failed to create Duktape context");
            //     return;
            // }
        }
    };

    class DukEnvironment : public DukContext
    {
      public:
        DukEnvironment(DukContext& ctx)
        {
            // TODO look into if context sharing the same global object is needed
            // duk_idx_t thread_index = duk_push_thread(m_ctx);
            //! pushes a context onto existing heap, and copies the global context (issue is that setting a global
            //! variable affects all scripts), i.e s1.js age = 10, s2.js age is then 10

            m_ctx = std::shared_ptr<duk_context>(
                [&]() {
                    // create a new context inside the same heap, but with a fresh set of global object
                    duk_idx_t thread_index = duk_push_thread_new_globalenv(ctx.GetContext());
                    return duk_require_context(ctx.GetContext(), thread_index);
                }(),
                [](duk_context* ctx) { duk_destroy_heap(ctx); });

            // TODO extract this into a function called CopyGlobalVars?
            //! register the global vars and functions
            duk_push_global_object(ctx.GetContext());
            duk_enum(ctx.GetContext(), -1, DUK_ENUM_OWN_PROPERTIES_ONLY);

            while (duk_next(ctx.GetContext(), -1, 0))
            {
                // Here, the top of the stack contains the property key
                const char* key = duk_safe_to_string(ctx.GetContext(), -1);

                // Fetch the associated value
                duk_get_global_string(ctx.GetContext(), key);

                // Move it to the new context
                duk_xmove_top(m_ctx.get(), ctx.GetContext(), 1);

                // And set it as a global in the new context
                duk_put_global_string(m_ctx.get(), key);

                // Clean up the key
                duk_pop(ctx.GetContext());
            }
        }
    };
} // namespace DukTie

namespace Fenrir
{
    class App;

    struct JSScript
    {
        DukTie::DukEnvironment env;
        std::string path;

        JSScript() = default;

        JSScript(DukTie::DukContext& ctx, const std::string& path) : env(ctx), path(path)
        {
        }

        JSScript(const JSScript& other) : env(other.env), path(other.path)
        {
        }
    };

    struct JSObject;

    using JSType = std::variant<int, double, bool, std::string, std::shared_ptr<JSObject>>;

    struct JSObject : public std::unordered_map<std::string, JSType>
    {
    };

    class ScriptEngine
    {
      public:
        ScriptEngine(Fenrir::ILogger& logger, const Time& time);

        JSScript CreateScript(const std::string& scriptPath); //? should this return a ref?

        JSType GetGlobal(const std::string& name); //? should this be made const ref?
        void SetGlobal(const std::string& name, const JSType& value);

        JSType GetScriptVariable(const JSScript& script,
                                 const std::string& varName); //? should this be made const ref?
        void SetScriptVariable(const JSScript& script, const std::string& varName, const JSType& value);

        void Update(Fenrir::App& app);

        void Tick(Fenrir::App& app);

      private:
        DukTie::DukState m_ctx; //! the global context
        std::vector<duk_context*> m_scriptCtxs;
        std::vector<JSScript> m_scripts; //! just for testing - remove later

        Fenrir::ILogger& m_logger;

        const Time& m_time; // ! testing

        void PushArg(duk_context* ctx, const JSType& arg);
        JSType GetVariable(duk_context* ctx, const std::string& varName);
        void SetVariable(duk_context* ctx, const std::string& varName, const JSType& value);

        void RunFile(duk_context* ctx, const std::string& file);
        void RunFunction(duk_context* ctx, const std::string& funcName,
                         const std::vector<JSType>& args); //! this runs using global context - probably not ideal

        void RegisterFunctions(duk_context* ctx);
        void RegisterVars(duk_context* ctx);

        template <typename T>
        void SetGlobal(const std::string& name, T* value)
        {
            m_ctx.SetVar(name, value);
        }

        template <typename T>
        void SetGlobal(const std::string& name, const T* value)
        {
            m_ctx.SetVar(name, value);
        }
    };

} // namespace Fenrir

#define BIND_SCRIPT_ENGINE_FN(fn, scriptEngineInstance) \
    std::bind(&Fenrir::ScriptEngine::fn, &scriptEngineInstance, std::placeholders::_1)
