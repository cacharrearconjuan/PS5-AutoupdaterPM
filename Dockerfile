FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

# Instalamos las herramientas básicas
RUN apt-get update && apt-get install -y \
    curl wget git make jq xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Leemos la web (no la API) para esquivar los bloqueos de GitHub y extraemos el enlace exacto
RUN DOWNLOAD_PATH=$(curl -sL https://github.com/john-tornblom/ps5-payload-sdk/releases/latest | grep -Eo '/john-tornblom/ps5-payload-sdk/releases/download/[^"]+\.tar\.xz' | head -n 1) && \
    if [ -z "$DOWNLOAD_PATH" ]; then echo "Error: No se pudo encontrar el enlace"; exit 1; fi && \
    SDK_URL="https://github.com$DOWNLOAD_PATH" && \
    echo "Descargando SDK desde: $SDK_URL" && \
    wget -q "$SDK_URL" -O /tmp/sdk.tar.xz && \
    tar -xf /tmp/sdk.tar.xz -C /opt/ && \
    rm /tmp/sdk.tar.xz

# Configuramos la ruta para que Makefile lo encuentre
ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
WORKDIR /workspace
COPY . /workspace

# Compilamos
CMD ["make", "clean", "all"]