Markdown
# PS5 Payload Manager Autoloader & Updater

Un payload para PlayStation 5 diseñado para actualizar **PS5 Payload Manager** e instalarlo automáticamente en la ruta `/data/ps5_autoloader` de la consola.

---

## 📌 Descripción

Este proyecto automatiza el despliegue y actualización de Payload Manager en consolas PS5. Al ubicar y configurar los binarios en `/data/ps5_autoloader`, se asegura de que el gestor de payloads esté listo para ejecutarse de forma automática al iniciar los entornos homebrew/exploits compatibles.

---

## 🚀 Características

* **Actualización sencilla:** Copia y reemplaza los archivos necesarios de Payload Manager a la versión más reciente.
* **Configuración Autoloader:** Ubica los binarios directamente en `/data/ps5_autoloader/` para su ejecución automática.
* **Gestión de archivos:** Crea las estructuras de directorios requeridas en `/data/` si no existen previamente.

---

## 📋 Requisitos Previos

* Consola PS5 con capacidad para ejecutar payloads (exploit activo).
* **[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk)** configurado si planeas compilar el binario desde el código fuente.

---

## 🛠️ Compilación

1. Configura la variable de entorno de tu SDK:
   ```bash
   export PS5_PAYLOAD_SDK=/ruta/a/tu/sdk
Compila el binario:

Bash
make
Limpia el entorno de compilación (opcional):

Bash
make clean
El resultado generará el archivo ejecutable .elf listo para ser enviado a la PS5.

📖 Modo de Uso
Envía el payload .elf a tu PS5 mediante tu método habitual (Payload Sender, Netcat en el puerto correspondiente, etc.).

El payload creará la carpeta /data/ps5_autoloader (si no existe) y copiará la versión actualizada de Payload Manager.

A partir de ese momento, el autoloader de tu consola detectará y ejecutará Payload Manager automáticamente al iniciar el entorno.

🙏 Créditos y Agradecimientos
itsPLK por la creación y mantenimiento de ps5-payload-manager.

A los desarrolladores y colaboradores del PS5 Payload SDK y la comunidad de la escena PS5.