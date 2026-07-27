#!/bin/bash
echo "Iniciando compilación en contenedor Docker..."
docker build -t ps5-autoupdater-builder .
docker run --rm -v "$(pwd):/workspace" ps5-autoupdater-builder
echo "Compilación finalizada."
