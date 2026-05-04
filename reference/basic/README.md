# GRPC basic synchronous put-get examples

The synchronous (blocking) client-server pair demonstrates 
a service and data structures constructed using ProtoBuff 
and gRPC wrappers.

Its intended purpuse is to provide the core foundation that
is used in building complex distributed systems. 

Yes, the three methods (ping, put, and get) are trivial. It
nevertheless provides all that is needed to setup exchanges
of information. What is embodied in the information and the 
control logic/states define the complexity of the system; the
mechanics of the calls are not complex.

The lab consists of the following implementations


|         | python | C++    | java |
|:---------|:--------:|:--------:|:--------:|
| Client  |   Y    |   Y    | |
| Server  |        |   Y    | |


## Python client 

A python client is provided

### Required packages

You will need to install (upgrade) the following packages:

   * python3 -m pip install --upgrade pip
   * python3 -m pip install grpcio
   * python3 -m pip install grpcio-tools

You may want to consider using a virtual environment to isolate
the gRPC package/tools. There are several to choose from.

   * venv, https://docs.python.org/3/library/venv.html, 
           https://python.land/virtual-environments
   * virtualenv, https://pypi.org/project/virtualenv/

For instance using venv:

   ```bash
   mkdir my-venv
   python3 -m venv ./my-venv
   source my-venv/bin/activate

   python3 -m pip install --upgrade pip
   python3 -m pip install grpcio
   python3 -m pip install grpcio-tools
   ```

Once installed, you can use the virtual env to build the python client.
   ```bash
   source my-venv/bin/activate

   cd basic/python
   ./build.sh

   # do what you need to do and when done:

   deactivate
   ```

External references discussing installation:

   * https://python.land/virtual-environments 
   * https://help.pythonanywhere.com/pages/VirtualEnvForWebsites/


### Building with Python

```
mkdir py
cd py
python3 -m grpc_tools.protoc -I../resources --python_out=. --pyi_out=. --grpc_python_out=. ../resources/basic.proto
```

## Python server

Using the imortalized words, this is left up to the student. 
This is not a difficult endeavor, it is a useful task to 
navigate the wealth of gRPC's examples on gitHub 
(https://github.com/grpc).


## C++ client and server

### Building 

Like previous labs this lab uses Cmake. Unlike other labs that
only compiles and links, it builds the C++ grpc service classes 
and stub interfaces with an external call to the protoc binary/
tool. Protoc generates the classes and server stubs.

Note there are path specific values in the CMakeLIsts.txt file.


### C++ Required packages

   * Using brew, yum, or apt install grpc
   * download the source code for gRPC from github, we are after the 
     Module and examples directory
   * cmake

The **examples directory** contains a great resource on how to build 
features of gRPC across multiple languages. 

The **modules directory** provides the rules for discovering packages and 
provides a learning point to understand how to write your own custom 
finder.


### Building

For platform/OS specific requirements refer below. Otherwise, using CMake:

```
mkdir b
cd b
cmake ..
make
```

The call to `cmake ..` will generate the files from the `.proto` file in
the command `add_custom_command`. You can also run the command using 
(build-proto.sh):

```
#!/bin/sh

# source code directory - we need an absolute path to the .protoc otherwise,
# the generated files are nested in ${base}/generated with the relative path.

base="/Users/gash/workspace-cpp/grpc/basic"
mkdirs(${base}/b/generated)

protoc --cpp_out ${base}/b/generated --grpc_out ${base}/b/generated \
       -I ${base}/resources \
       --plugin=protoc-gen-grpc=/opt/homebrew/Cellar/grpc/1.66.2/bin/grpc_cpp_plugin \
       ${base}/resources/loop.proto
```

### Windows Building

Visual Studio is your best bet here. VS already includes openmp.


### MAC OSX Building

If you have used brew to install your C++ compiler, grpc, protobuf, 
and possibily built other packages using gcc-x (GNUs not XCode) then
there is a likelihood that you are mixing libraries built with 
XCode (brew) and GNU.

This mixing is bad because of name mangling between LLVM and GNU. Errors
during linking is a sign that you are mixing.

What to do:

 * Instruct brew to use GNU (or clang) only - CXX and CC environment variables
 * Build from source - this includes all dependencies
 * Build with XCode (see below) 


### Building w/ XCode (likely the easiest)

 * You will need ensure that you used brew to install grpc, libomp, and protobuf

### Linux

 * Use apt-git, yum, or rpm whichever is your package installer for the distro.

### Credit/References/Reading

 * https://grpc.io/docs/quickstart/python.html
 * https://grpc.io/docs/what-is-grpc/faq/
 * https://cmake.org/cmake/help/latest/command/add_custom_command.html
 * https://github.com/IvanSafonov/grpc-cmake-example
 
 * markdown highlighting https://github.com/highlightjs/highlight.js/blob/main/SUPPORTED_LANGUAGES.md
