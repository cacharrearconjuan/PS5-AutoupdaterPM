#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>

#define TARGET_DIR "/data/ps5_autoloader"
#define TARGET_ELF "/data/ps5_autoloader/pldmgr.elf"
#define TARGET_TXT "/data/ps5_autoloader/autoload.txt"
#define DOWNLOAD_URL "https://github.com/itsPLK/ps5-payload-manager/releases/latest/download/pldmgr.elf"

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

int main() {
    mkdir(TARGET_DIR, 0777);
    CURL *curl;
    FILE *fp;
    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        fp = fopen(TARGET_ELF, "wb");
        if (fp) {
            curl_easy_setopt(curl, CURLOPT_URL, DOWNLOAD_URL);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); 
            
            res = curl_easy_perform(curl);
            fclose(fp);
        }
        curl_easy_cleanup(curl);
    }

    int fd = open(TARGET_TXT, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd >= 0) {
        const char* contenido = "!3000\npldmgr.elf\n";
        write(fd, contenido, strlen(contenido));
        close(fd);
    }
    return 0;
}