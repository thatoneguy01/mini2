#include <iostream>
#include "wrapper.h"

/**
 * demo
 */
int main(int argc, char **argv) {
   std::cout << "A client" << std::endl;

   Wrapper w;
   auto good = w.setup("127.0.0.1",50051);
   if ( !good ) {
      std::cout << "could not connect to server" << std::endl;
      exit(-1);
   }

   // --------------------------------------------------
   // demonstrate ping
   //    - this helps to verify connections
   //    - can be used to calculate round trip time

   auto r = w.ping();
   std::cout << "test ping: " << (r ? "success":"failed") << std::endl;

   // --------------------------------------------------
   // demonstrate put 

   long id;
   r = w.put("color","red",id);
   if ( r ) 
      std::cout << "test put(color,red) ref: " << id << std::endl;
   else
      std::cout << "test put failed" << std::endl;

   // --------------------------------------------------
   // demonstrate get

   std::string name, value;
   r = w.get(id,name,value);
   if ( r ) 
      std::cout << "test get(" << id << ") " << name << "," << value << std::endl;
   else
      std::cout << "test get failed" << std::endl;
}
