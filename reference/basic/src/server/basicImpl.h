#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

// generated code
#include "basic.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;

using basic::BasicService;

// print test results
//#define VERBOSE
#define SHOWRESULTS

class BasicImpl final : public BasicService::Service {
  private:

  long mNextId;
  std::unordered_map<long, ::basic::Pair> mCache;
   
  public:

  explicit BasicImpl() : mNextId(0) {
  }

  ~BasicImpl() {
     mCache.clear();;
  }

  /**
   * service implementation
   */

  ::grpc::Status ping(::grpc::ServerContext* context, const ::google::protobuf::Empty* request, 
		      ::google::protobuf::Empty* response) override; 

  ::grpc::Status get(::grpc::ServerContext* context, const ::basic::Ref* request, 
		     ::basic::Pair* response) override;

  ::grpc::Status put(::grpc::ServerContext* context, const ::basic::Pair* request, 
		     ::basic::Ack* response) override;

  /**
   * service support 
   */

  bool setup();

  private: 
  long generateCacheId();
};
