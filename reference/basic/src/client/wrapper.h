#include <grpcpp/grpcpp.h>

#include "basic.grpc.pb.h"

/**
 * A wrapper (proxy) to the service is demonstrated to hide
 * package (gRPC) specific service coding, minimizing 
 * the polution of the gRPC technologies into your code.
 *
 * Notice that the wrapper does not expose the gRPC/protobuf
 * data structures
 */
class Wrapper {
private:
   std::unique_ptr<basic::BasicService::Stub> mClient;

public:
   bool setup(std::string address, uint port) {
      bool rtn = false;
   
      std::string target = std::format("{}:{}",address,port);
      std::cout  << "client connecting to " << target << std::endl;

      std::shared_ptr<grpc::Channel> channel =
            grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
      mClient = basic::BasicService::NewStub(channel);

      grpc_connectivity_state state = channel->GetState(true);
      std::cout << "Initial state: " << state << std::endl;

      // wait to ensure channel is active
      int tries = 5;
      while ( state != GRPC_CHANNEL_READY ) {
         if (tries == 0) break;
         tries--;
         sleep(2);
         state = channel->GetState(true);
         std::cout << "checking channel state: " << state << std::endl;
      }

      if ( state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
         state == GRPC_CHANNEL_SHUTDOWN ) {
         std::cout << "Channel failure: " << state << std::endl;
      } else if ( state == GRPC_CHANNEL_READY ) {
         std::cout << "Channel ready" << std::endl;
         return true;
      } else {
         std::cout << "Channel unknown state: " << state << std::endl;
      }
      std::cout.flush();

      return false;
   }

   bool ping() {
      ::google::protobuf::Empty request;
      ::google::protobuf::Empty reply;
      grpc::ClientContext context;
      ::grpc::Status status = mClient->ping(&context, request, &reply);

      return status.ok();
   }

   bool put(std::string n, std::string v, long &ref) {
      ::basic::Pair request;
      ::basic::Ack reply;
      grpc::ClientContext context;
      request.set_name(n);
      request.set_value(v); 
      ::grpc::Status status = mClient->put(&context, request, &reply);
      ref = reply.id();

      return status.ok();
   }

   /**
    * If you are reading this, you will note the call to the 
    * server is blocking. This is on purpose. Obviously you
    * maybe tempted to convert the server to use nonblocking 
    * calls. Don't. Nonblocking or streaming APIs remove the 
    * control you will need to effectly react to changing 
    * network conditions and the dynamic demands within your 
    * system of services/process. Second, you already know 
    * how to use threads. There is nothing magical about the 
    * non-blocking or streaming APIs.
    */
   bool get(long id, std::string &name, std::string &value) {
      ::basic::Ref request;
      ::basic::Pair reply;
      grpc::ClientContext context;
      request.set_id(id);
      ::grpc::Status status = mClient->get(&context, request, &reply);
      if ( status.ok() ) {
         name = reply.name();
         value = reply.value();
      } else {
         name = "";
         value = ""; 
      }

      return status.ok();
   }

};

