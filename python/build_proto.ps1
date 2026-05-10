param(
  [string]$ProtoPath = "..\proto\basecamp.proto",
  [string]$OutDir = ".\generated"
)

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if (-not [System.IO.Path]::IsPathRooted($ProtoPath)) {
  $ProtoPath = Join-Path $ScriptDir $ProtoPath
}
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
  $OutDir = Join-Path $ScriptDir $OutDir
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
python -m grpc_tools.protoc -I(Join-Path $ScriptDir "..\proto") --python_out=$OutDir --grpc_python_out=$OutDir $ProtoPath
Get-ChildItem -Path $OutDir -Filter basecamp_pb2_grpc.py | ForEach-Object {
  (Get-Content $_.FullName) -replace 'import basecamp_pb2 as basecamp__pb2', 'from generated import basecamp_pb2 as basecamp__pb2' |
    Set-Content $_.FullName
}
Write-Output "Generated Python gRPC files in $OutDir"
