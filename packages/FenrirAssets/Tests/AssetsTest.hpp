#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"

namespace AssetsTest
{
    // Keeps what was logged as errors, so a test can check a failure was reported. A second ConsoleLogger in one
    // process throws, so the tests never use one.
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> errors) : m_errors(std::move(errors))
        {
        }
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_errors;
    };

    // The directory holding the image fixtures, absolute.
    inline std::filesystem::path Fixtures()
    {
        return std::filesystem::absolute(FENRIR_ASSETS_FIXTURES);
    }

    // An App with assets installed, its asset root the fixtures directory unless told otherwise. Members end in reverse
    // order, so the owner is destroyed before the App, as it must be.
    struct AssetsApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors)};
        Fenrir::Assets assets;
        Fenrir::AssetStore& store;

        explicit AssetsApp(Fenrir::AssetsConfig config = {Fixtures()})
            : assets(Fenrir::Assets::Install(app, std::move(config))), store(assets.Store())
        {
        }
    };

    // A triangle in the XY plane, from (-1, 0, 0) to (2, 3, 0), with a normal per vertex.
    inline Fenrir::MeshData Triangle()
    {
        Fenrir::MeshData mesh;
        mesh.positions = {{-1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}};
        mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
        mesh.indices = {0, 1, 2};
        mesh.submeshes = {{0, 3, 0}};
        return mesh;
    }
} // namespace AssetsTest
