#include "basicImpl.h"

// print test results
//#define VERBOSE
#define SHOWRESULTS

::grpc::Status BasicImpl::ping(::grpc::ServerContext* context, const ::google::protobuf::Empty* request,
                      ::google::protobuf::Empty* response) {
    std::cout << "server ping()" << std::endl; std::cout.flush();
    std::cout.flush();
    return grpc::Status::OK;
}

::grpc::Status BasicImpl::get(::grpc::ServerContext* context, const ::basic::Ref* request, 
		              ::basic::Pair* response) {
#ifdef SHOWRESULTS
    std::cout << "basic::get() " << request->id() << std::endl;
#endif

    if ( mCache.contains(request->id()) ) {
       auto p = mCache[request->id()];
       response->set_name(p.name());
       response->set_value(p.value());
    } else {
       response->set_id(-1);
       //response->set_name("");
       //response->set_value("");
    }

    return ::grpc::Status::OK;
}

::grpc::Status BasicImpl::put(::grpc::ServerContext* context, const ::basic::Pair* request, 
		              ::basic::Ack* response) {
#ifdef SHOWRESULTS
    std::cout << "basic::put() " << request->name() << std::endl;
#endif

    ::basic::Pair p = *request;
    p.set_id(++mNextId);
    auto r = mCache.try_emplace(p.id(),p);

    response->set_id(p.id());
    response->set_success(r.second);

    return ::grpc::Status::OK;
}

// -----------------------------------------------------

bool BasicImpl::setup() {
#ifdef SHOWRESULTS
    printf("basic::setup() configuring service\n");
#endif

   return true;
}
