#include "application/rpcapplication.h"
#include "comm/config.h"
#include "comm/log.h"
#include "comm/process_signal.h"
#include <iostream>
#include <unistd.h>
#include <string>

void ShowArgsHelp()
{
  std::cout << "format: command -i <configfile>" << std::endl;
}

void RpcApplication::Init(int argc, char **argv)
{
  if (argc < 2)
  {
    ShowArgsHelp();
    exit(EXIT_FAILURE);
  }

  int c = 0;
  std::string config_file;
  while ((c = getopt(argc, argv, "i:")) != -1)
  {
    switch (c)
    {
    case 'i':
      config_file = optarg;
      break;
    case '?':
      ShowArgsHelp();
      exit(EXIT_FAILURE);
    case ':':
      ShowArgsHelp();
      exit(EXIT_FAILURE);
    default:
      break;
    }
  }

  if (config_file.empty())
  {
    ShowArgsHelp();
    exit(EXIT_FAILURE);
  }

  if (!crpc::GetConfig()->loadFromFile(config_file))
  {
    std::cerr << "load config failed: " << config_file << std::endl;
    exit(EXIT_FAILURE);
  }

  if (!crpc::PrepareProcessSignals())
  {
    std::cerr << "prepare process signals failed" << std::endl;
    exit(EXIT_FAILURE);
  }

  crpc::InitLogger();

  if (!crpc::StartSignalWaiter())
  {
    std::cerr << "start signal waiter failed" << std::endl;
    crpc::ShutdownLogger();
    exit(EXIT_FAILURE);
  }
}

RpcApplication &RpcApplication::GetInstance()
{
  static RpcApplication app;
  return app;
}
