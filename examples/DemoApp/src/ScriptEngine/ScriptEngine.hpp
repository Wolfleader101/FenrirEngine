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

    template <typename ClassType, typename PropType, PropType ClassType::*Prop>
    class PropertyWrapper
    {
      public:
        static PropType Get(const ClassType* obj)
        {
            return obj->*Prop;
        }

        static void Set(ClassType* obj, PropType value)
        {
            obj->*Prop = value;
        }
    };

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
            dukglue_register_constructor<T>(m_ctx, m_typeName.c_str());
        }

        /**
         * @brief Set a property on the object based on a member pointer
         *
         * @tparam T type of the object
         * @tparam V member pointer type
         * @param propName name of the property
         * @param memberPtr pointer to the member
         */
        template <typename T, typename V>
        void SetProperty(const std::string& propName, V T::*memberPtr)
        {
            // std::string hiddenPropName = "\xff\xff" + propName; // hidden property name

            // push the property name
            duk_push_string(m_ctx, propName.c_str());

            // // push the getter function (which gets the member value)
            // duk_push_c_function(m_ctx, &TemplateMemberGetter<T, V>, 1);

            // // alloc a buffer to store the member pointer (so it can be retrieved in the getter function)
            // V T::** ptrStore = static_cast<V T::**>(duk_push_fixed_buffer(m_ctx, sizeof(memberPtr)));
            // *ptrStore = memberPtr; // store the member pointer in the buffer

            // // Store the buffer in the function's "\xff\xff<prop>"" property that isnt exposed to JS
            // // duk_put_prop_string(m_ctx, -2, hiddenPropName.c_str());
            // duk_put_prop_string(m_ctx, -2,
            //                     "\xff"
            //                     "\xff"
            //                     "data");

            // // Define the property with the getter
            // duk_def_prop(m_ctx, m_objIdx, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_CONFIGURABLE);

            // push the getter function (which gets the member value)
            duk_push_c_function(m_ctx, TemplateMemberGetter<T, V>,
                                1); // nargs is 1 because the getter takes one argument: 'this'

            // alloc a buffer to store the member pointer (so it can be retrieved in the getter function)
            void* buffer = duk_push_fixed_buffer(m_ctx, sizeof(memberPtr));
            std::memcpy(buffer, &memberPtr, sizeof(memberPtr)); // store the member pointer in the buffer
            // duk_put_prop_string(m_ctx, -2, propName.c_str());
            duk_put_prop_string(m_ctx, -2,
                                "\xff"
                                "\xff"
                                "data");

            duk_def_prop(m_ctx, m_objIdx, DUK_DEFPROP_HAVE_GETTER | DUK_DEFPROP_SET_CONFIGURABLE);
        }

      private:
        duk_context* m_ctx;
        std::string m_typeName;
        duk_idx_t m_objIdx;

        template <typename T, typename V>
        static duk_ret_t TemplateMemberGetter(duk_context* ctx)
        {
            duk_push_current_function(ctx);
            duk_get_prop_string(ctx, -1,
                                "\xff"
                                "\xff"
                                "data");
            V T::*memberPtr;
            std::memcpy(&memberPtr, duk_get_buffer_data(ctx, -1, nullptr), sizeof(memberPtr));
            duk_pop_2(ctx); // Pop off the function and the buffer from the stack

            T* obj = static_cast<T*>(duk_get_heapptr(ctx, 0)); // Get the 'this' pointer from the top of the stack
            if (!obj)
                return DUK_RET_TYPE_ERROR; // Return an error if the object is not found

            V value = obj->*memberPtr; // Access the property value
            if constexpr (std::is_same<V, std::string>::value)
            {
                duk_push_string(ctx, value.c_str());
            }
            else
            {
                duk_push_number(ctx, static_cast<double>(value)); // Push the property value as a number
            }
            return 1; // Return one value (the property value)

            // auto ptr = static_cast<T*>(duk_to_pointer(ctx, 0));
            // V T::*memberPtr;
            // std::memcpy(&memberPtr, duk_get_buffer_data(ctx, -1, nullptr), sizeof(memberPtr));

            // if constexpr (std::is_same<V, std::string>::value)
            // {
            //     duk_push_string(ctx, (ptr->*memberPtr).c_str());
            // }
            // else
            // {
            //     duk_push_number(ctx, ptr->*memberPtr);
            // }
            // return 1;

            // duk_push_current_function(ctx);
            // duk_get_prop_string(ctx, -1,
            //                     "\xff"
            //                     "\xff"
            //                     "data");
            // V T::** ptrStore = static_cast<V T::**>(duk_get_buffer_data(ctx, -1, nullptr));
            // V T::*memberPtr = *ptrStore;
            // duk_pop_2(ctx);

            // T* obj = static_cast<T*>(duk_get_heapptr(ctx, 0));
            // if (!obj)
            //     return DUK_RET_TYPE_ERROR;

            // V value = obj->*memberPtr;
            // if constexpr (std::is_same<V, std::string>::value)
            // {
            //     duk_push_string(ctx, value.c_str());
            // }
            // else
            // {
            //     duk_push_number(ctx, static_cast<double>(value));
            // }
            // return 1;
        }
    };

} // namespace Fenrir

#define BIND_SCRIPT_ENGINE_FN(fn, scriptEngineInstance) \
    std::bind(&Fenrir::ScriptEngine::fn, &scriptEngineInstance, std::placeholders::_1)
