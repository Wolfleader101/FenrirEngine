#pragma once

// Types the tool refuses, one per diagnostic. The tests select each one alone and check the diagnostic it gives.

namespace Failing
{
    float Scale();

    struct UnknownDefault
    {
        float scale = Scale();
    };

    class PrivateMember
    {
      public:
        float visible = 1.0f;

      private:
        float m_hidden = 0.0f;
    };

    struct ReferenceMember
    {
        float& target;
    };

    struct VirtualFunction
    {
        virtual void Update();
        float value = 0.0f;
    };

    struct Base
    {
        float inherited = 0.0f;
    };

    struct BaseWithData : Base
    {
        float own = 0.0f;
    };

    struct Copied
    {
        Copied() = default;
        Copied(const Copied& other);
        float value = 0.0f;
    };

    struct NonTriviallyCopyableMember
    {
        Copied member;
    };

    struct Inexpressible
    {
        long double wide = 0.0L;
    };

    struct NoInitialiser
    {
        float value;
    };

    struct Pair
    {
        Pair() = default;

        Pair(float a, float b) : first(a), second(b)
        {
        }

        float first, second;
    };

    struct UnnamedArguments
    {
        Pair pair = Pair(1.0f, 2.0f);
    };
} // namespace Failing
