#
# client to our c++ server.
#
# Reading:
# https://grpc.io/docs/languages/python/basics/

import grpc
import basic_pb2
import basic_pb2_grpc

from google.protobuf.empty_pb2 import Empty

def run():
    try:
       channel = grpc.insecure_channel('localhost:50051')
       stub = basic_pb2_grpc.BasicServiceStub(channel)

       # ping test
       print("\nping test:")
       response = stub.ping(Empty())
       #response = stub.ping(Empty(),wait_for_ready=True,timeout=2000)
       dir(response) # should there be one?

       # put-get test is similar to the c++ client
       print("\nput-get test: storing pair (color.py,red) on server")
       reqPut = basic_pb2.Pair()
       reqPut.id = 1
       reqPut.name = "color.py"
       reqPut.value = "red"
       respPut = stub.put(reqPut)
       dir(respPut)
       if respPut.success == 0:
           print("   put:", (1==respPut.success), ", error = ", respPut.errorCode, ": ", respPut.errorText)
       else: # success
          print("   put:", (1==respPut.success), ", id = ", respPut.id)
          print("\nput-get test: getting the server value")
          reqGet = basic_pb2.Ref()
          reqGet.id = respPut.id
          respGet = stub.get(reqGet)
          #dir(respGet)
          print("   get: id = ", respGet.id, ", name = ", respGet.name, ", value = ", respGet.value)
    except:
       print("\n\n** failed to connect/send to service **\n\n")

       # uncomment to see trace
       #raise


if __name__ == '__main__':
    run()
