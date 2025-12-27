#include "sound_exclusion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VIRTUAL_SINK_NAME "GStreamer_Yayin"
#define VIRTUAL_SINK_DESC "Yayin_Icin_Otomatik_Kanal"
#define BUFFER_SIZE 1024

char original_sink[256] = {0};
int null_module_id = -1;
int loop_module_id = -1;

void run_command_output(const char *cmd, char *result, int max_len) {
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("Hata: Komut çalıştırılamadı -> %s\n", cmd);
        return;
    }
    if (fgets(result, max_len - 1, fp) != NULL) {
        // Sondaki enter karakterini sil
        result[strcspn(result, "\n")] = 0;
    }
    pclose(fp);
}

void run_command(const char *cmd) {
    system(cmd);
}

int load_module(const char *cmd) {
    char buffer[64];
    run_command_output(cmd, buffer, sizeof(buffer));
    return atoi(buffer);
}

typedef struct {
    int id;
    char name[256];
} AppInfo;

void restore_system() {
    printf("\n[*] Sistem eski haline getiriliyor...\n");
    
    if (strlen(original_sink) > 0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "pactl set-default-sink %s", original_sink);
        run_command(cmd);
        printf(" -> Varsayılan çıkış geri yüklendi: %s\n", original_sink);
    }

    if (null_module_id != -1) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "pactl unload-module %d", null_module_id);
        run_command(cmd);
        printf(" -> Sanal kanal silindi (ID: %d)\n", null_module_id);
    }

    if (loop_module_id != -1) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "pactl unload-module %d", loop_module_id);
        run_command(cmd);
        printf(" -> Loopback silindi (ID: %d)\n", loop_module_id);
    }

    printf("[OK] Çıkış yapıldı.\n");
}

void get_excluded_sound() {
    restore_system();
    run_command_output("pactl get-default-sink", original_sink, sizeof(original_sink));
    printf("[*] Mevcut Fiziksel Çıkış: %s\n", original_sink);

    if (strcmp(original_sink, VIRTUAL_SINK_NAME) == 0) {
        printf("HATA: Zaten sanal kanal varsayılan. Lütfen önce sistemi resetleyin.\n");
        return;
    }

    printf("[*] Sanal kanal oluşturuluyor...\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "pactl load-module module-null-sink sink_name=%s sink_properties=device.description=%s", VIRTUAL_SINK_NAME, VIRTUAL_SINK_DESC);
    null_module_id = load_module(cmd);

    printf("[*] Loopback açılıyor...\n");
    snprintf(cmd, sizeof(cmd), "pactl load-module module-loopback source=%s.monitor sink=%s", VIRTUAL_SINK_NAME, original_sink);
    loop_module_id = load_module(cmd);

    printf("[*] Varsayılan çıkış SANAL KANAL yapılıyor...\n");
    snprintf(cmd, sizeof(cmd), "pactl set-default-sink %s", VIRTUAL_SINK_NAME);
    run_command(cmd);

    printf("-----------------------\n");


    char input_buffer[256];
    printf("\nSes Kaynakları:\n");
    system("pactl list sink-inputs | grep -E 'Sink Input #|application.name'");
    printf("\n[Komut Bekleniyor]\n");
    printf("1. 'exclude {id}' -> ID'si verilenleri fiziksel karta at (Yayından gizle)\n");
    printf("2. 'exit'  -> Sistemi eski haline getir ve çık\n");
    printf("> ");

    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) return;
    
    input_buffer[strcspn(input_buffer, "\n")] = 0;

    if (strcmp(input_buffer, "exit") == 0) {
        restore_system();
    } 
    else if (strncmp(input_buffer, "exclude ", 8) == 0) {
        char *token = strtok(input_buffer + 8, " ");
        while (token != NULL) {
            int app_id = atoi(token);
            if (app_id > 0) {
                printf(" -> Uygulama %d Fiziksel Karta taşınıyor...\n", app_id);
                snprintf(cmd, sizeof(cmd), "pactl move-sink-input %d %s", app_id, original_sink);
                run_command(cmd);
            }
            token = strtok(NULL, " ");
        }
    }
    else {
            printf("Bilinmeyen komut. 'list', 'exclude ID' veya 'exit' yazın.\n");
    }

    return;
}