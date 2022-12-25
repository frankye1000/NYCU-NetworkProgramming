#include "socks.h"


int main(int argc, char* argv[])
{
	try
	{
		if (argc != 2)
		{
			std::cerr << "Errpr: Server arg error\n";
			return 1;
		}

		boost::asio::io_context io_context;

		Server s(io_context, std::atoi(argv[1]));

		io_context.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Error: " << e.what() << "\n";
	}

	return 0;
}
