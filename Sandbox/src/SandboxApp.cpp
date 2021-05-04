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

int main()
{
	Sandbox* sandbox = new Sandbox();
	sandbox->Run();
	delete sandbox;
}