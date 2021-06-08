#include <Fenrir.h>

class ExampleLayer : public Fenrir::Layer
{
public:
	ExampleLayer()
		: Layer("Example")
	{
	}

	void OnUpdate() override
	{
		FE_INFO("ExampleLayer::Update");
	}

	void OnEvent(Fenrir::Event& event) override
	{
		FE_TRACE("{0}", event);
	}

};

class Sandbox : public Fenrir::Application
{
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer());

	}

	~Sandbox()
	{

	}
};

Fenrir::Application* Fenrir::CreateApplication()
{
	return new Sandbox();
}