#pragma once

#include <duktape.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirTime/Time.hpp"

namespace Fenrir
{
    class App;

    class DukContext

    {
      public:
        /**
         * @brief Construct a new Duk Context object by creating a new heap
         *
         * It creates a new heap and pushes context onto it
         */
        DukContext() : m_ctx(duk_create_heap(NULL, NULL, NULL, NULL, fatal_callback))
        {
            // TODO error handling
            // if (!m_ctx)
            // {
            //     m_logger.Fatal("Failed to create Duktape context");
            //     return;
            // }
        }

        /**
         * @brief Copy constructor for Duk Context
         *
         * It creates a new heap and pushes context onto it (deep copy of global context)
         *
         * @param other the other Duk Context to deeply copy.
         */
        DukContext(const DukContext& other)
        {
            // TODO look into if cojntext sharing the same global object is needed

            // To create a new context inside the same heap, but with a fresh set of global object
            duk_idx_t thread_index = duk_push_thread_new_globalenv(other.m_ctx);
            m_ctx = duk_require_context(other.m_ctx, thread_index);
        };

        /**
         * @brief Move constructor for Duk Context
         *
         * It moves the context from the other Duk Context to this one
         *
         * @param other the other Duk Context to move from
         */
        DukContext(DukContext&& other) : m_ctx(other.m_ctx)
        {
            other.m_ctx = nullptr;
        }

        ~DukContext()
        {
            if (m_ctx != nullptr)
                duk_destroy_heap(m_ctx);
        }

        template <typename RetType, typename... Ts>
        void SetFunction(const std::string& funcName, RetType (*funcToCall)(Ts...))
        {
            dukglue_register_function(m_ctx, funcToCall, funcName.c_str());
        }

        template <typename T>
        DukType<T> CreateType(const std::string& typeName)
        {
            return DukType<T>(m_ctx, typeName);
        }

        template <typename T>
        void SetVar(const std::string& varName, T* value)
        {
            // Push the object to Duktape stack
            dukglue_push(m_ctx, value);

            // Stash the object in the global object
            duk_put_global_string(m_ctx, varName.c_str());
        }

      protected:
        duk_context* m_ctx;
    };

    struct JSScript
    {
        duk_context* env_ctx;
        std::string path;
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
        ~ScriptEngine();

        JSScript CreateScript(const std::string& scriptPath); //? should this return a ref?

        JSType GetGlobal(const std::string& name); //? should this be made const ref?
        void SetGlobal(const std::string& name, const JSType& value);

        JSType GetScriptVariable(const JSScript& script,
                                 const std::string& varName); //? should this be made const ref?
        void SetScriptVariable(const JSScript& script, const std::string& varName, const JSType& value);

        void Update(Fenrir::App& app);

        void Tick(Fenrir::App& app);

      private:
        duk_context* m_ctx; //! the global context
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
            // Push the object to Duktape stack
            dukglue_push(m_ctx, value);

            // Stash the object in the global object
            duk_put_global_string(m_ctx, name.c_str());
        }

        template <typename T>
        void SetGlobal(const std::string& name, const T* value)
        {
            // Push the object to Duktape stack
            dukglue_push(m_ctx, value);

            // Stash the object in the global object
            duk_put_global_string(m_ctx, name.c_str());
        }
    };

    template <typename T>
    class DukType
    {
      public:
        /**-
         * @brief Construct a new Duk Type object
         *
         * @param ctx duk context to use
         * @param typeName of the object
         */
        DukType(duk_context* ctx, const std::string& typeName) : m_ctx(ctx), m_typeName(typeName)
        {
            // dukglue_register_constructor<T>(m_ctx, m_typeName.c_str());
            dukglue_register_function
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

} // namespace Fenrir

#define BIND_SCRIPT_ENGINE_FN(fn, scriptEngineInstance) \
    std::bind(&Fenrir::ScriptEngine::fn, &scriptEngineInstance, std::placeholders::_1)
