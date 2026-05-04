#!/bin/sh

# your protoc must match version numbers
PROTOC=/opt/homebrew/bin/protoc

# output generated code to
GENOUT=./b

echo "creating output to $GENOUT"

if [ ! -d $GENOUT ]; then
  mkdir $GENOUT
  touch ${GENOUT}/__init__.py
fi

cp ./src/basic_client.py $GENOUT/.

cd basic

# run the codegen to build the python stubs
python3 -m grpc_tools.protoc -I../../resources --python_out=. --pyi_out=. \
        --grpc_python_out=. ../../resources/basic.proto
