#include <iostream>

#include "basicImpl.h"

/**
 *
 */
void RunServer() {
  // TODO retrieve from a configuration
  //std::string server_address("127.0.0.1:50051");
  std::string server_address("0.0.0.0:50051");

  BasicImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  std::cout << "--------------------------------------------" << std::endl;
#if defined(_OPENMP)
  std::cout << "Server configured to use openmp" << std::endl;
#else
  std::cout << "Server is not threaded" << std::endl;
#endif
 
  auto good = service.setup();
  if ( good ) {
     std::cout << "Server listening on " << server_address << std::endl;
  } else {
     std::cout << "Server failed to setup...closing" << std::endl;
  }

  std::cout << "--------------------------------------------" << std::endl << std::endl;
  std::cout.flush();

  if (good) {
     std::cout << "Server ready." << std::endl << std::endl;
     std::cout.flush();
     server->Wait();
  }
}

/**
 *
 */
int main(int argc, char** argv) {
  RunServer();

  return 0;
}

