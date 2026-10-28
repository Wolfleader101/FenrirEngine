// Stand-ins for the natives the fixture's mirrors call: Body's generated part holds the methods the @facade marks naming
// it give it, and those call the natives' class, which has no bodies until the SDK's generator writes them. The checks
// that compile without the SDK (the stub check and the fixture's runtime assembly) compile this class in its place; the
// SDK check compiles the generated one.

namespace Fixture.Bindings
{
    internal static class FixtureNative
    {
        public static bool Outweighs(in Body body, float mass) => body.Mass > mass;

        public static bool Make(out Body body, float mass)
        {
            body = new() { Mass = mass };
            return mass > 0;
        }
    }
}
