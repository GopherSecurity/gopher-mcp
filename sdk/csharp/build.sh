#!/bin/bash

DIR_CURR=$(cd "$(dirname "$0")";pwd)
cd $DIR_CURR

dotnet restore
dotnet build
dotnet test