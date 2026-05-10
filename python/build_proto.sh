#!/bin/bash

# Defaults
ProtoPath="../proto/basecamp.proto"
OutDir="./generated"

# Get the directory where this script is located
ScriptDir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Convert relative paths to absolute
if [[ ! "$ProtoPath" = /* ]]; then
  ProtoPath="$ScriptDir/$ProtoPath"
fi
if [[ ! "$OutDir" = /* ]]; then
  OutDir="$ScriptDir/$OutDir"
fi

# Create output directory
mkdir -p "$OutDir"

# Run protoc
ProtoDir="$ScriptDir/../proto"
python -m grpc_tools.protoc -I"$ProtoDir" --python_out="$OutDir" --grpc_python_out="$OutDir" "$ProtoPath"

# Fix the import statement
if [ -f "$OutDir/basecamp_pb2_grpc.py" ]; then
  sed -i 's/import basecamp_pb2 as basecamp__pb2/from generated import basecamp_pb2 as basecamp__pb2/' "$OutDir/basecamp_pb2_grpc.py"
fi

echo "Generated Python gRPC files in $OutDir"
