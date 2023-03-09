#include "DefaultHTTPService.h"

#include "TeleportCore/ErrorHandling.h"
#include "TeleportServer/ClientData.h"
#include "TeleportServer/ServerSettings.h"

// Doesn't link properly when WebRTC is linked...
//#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

using namespace teleport;
using namespace server;

DefaultHTTPService::DefaultHTTPService()
	: mUsingSSL(false)
{

}

DefaultHTTPService::~DefaultHTTPService()
{
	shutdown();
}

bool DefaultHTTPService::initialize(std::string mountDirectory, std::string certPath, std::string privateKeyPath, int32_t port)
{
	if (mountDirectory == "")
	{
		TELEPORT_CERR << "Path of directory to mount cannot be empty!" << "\n";
		return false;
	}

	if (port == 0)
	{
		TELEPORT_CERR << "HTTP port cannot be 0!" << "\n";
		return false;
	}

	if (certPath.size() > 0 && privateKeyPath.size() > 0)
	{
		TELEPORT_COUT << "SSL certificate and key provided. Usng HTTPS server." << "\n";
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
		mServer.reset(new httplib::SSLServer(certPath.c_str(), privateKeyPath.c_str()));
		mUsingSSL = true;
#endif
	}
	else
	{
		TELEPORT_COUT << "SSL certificate and key not provided. Usng HTTP server." << "\n";
		mServer.reset(new httplib::Server());
		mUsingSSL = false;
	}



	// Mount / to provided directory
	auto ret = mServer->set_mount_point("/", mountDirectory);
	if (!ret)
	{
		TELEPORT_CERR << "Provided mount directory does not exist!" << "\n";
		return false;
	}

	// TODO: Pass these extra mappings in as parameters.
	mServer->set_file_extension_and_mimetype_mapping("texture", "image/texture");
	mServer->set_file_extension_and_mimetype_mapping("mesh", "image/mesh");
	mServer->set_file_extension_and_mimetype_mapping("material", "image/material");
	mServer->set_file_extension_and_mimetype_mapping("basis", "image/basis");

	mServer->bind_to_port("0.0.0.0", port);

	if (!mThread.joinable())
	{
		mThread = std::thread(&httplib::Server::listen_after_bind, mServer.get());
	}

	return true;
}

void DefaultHTTPService::shutdown()
{
	if (!mServer)
		return;

	mServer->stop();
	if (mThread.joinable())
	{
		mThread.join();
	}
	mServer.reset();

	mUsingSSL = false;
}

void DefaultHTTPService::tick()
{

}
