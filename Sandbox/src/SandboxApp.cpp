#include <Fenrir.h>

class Sandbox : public Fenrir::Application
{
public:
	Sandbox()
	{

	}

	~Sandbox()
	{

	}
};

Fenrir::Application* Fenrir::CreateApplication()
{
	return new Sandbox();
}