#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h> // Dodato za MISRA tipove podataka

// KERNEL INCLUDES
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

// HARDWARE SIMULATOR UTILITY FUNCTIONS
#include "HW_access.h"

// SERIAL SIMULATOR CHANNELS
#define COM_CH_0 (0)
#define COM_CH_1 (1)
#define COM_CH_2 (2)
#define R_BUF_SIZE (64)
#define REC_BUF_50 (50)

typedef float float_t;

// TASK PRIORITIES
#define TASK_SERIAl_REC_PRI_kanali ( tskIDLE_PRIORITY + 4 )
#define TASK_SERIAl_REC_PRI ( tskIDLE_PRIORITY + 3 )
#define SERVICE_TASK_PRI ( tskIDLE_PRIORITY + 2 )
#define OBRADA_PRI ( tskIDLE_PRIORITY + 1 )

// GLOBAL OS-HANDLES
static SemaphoreHandle_t RXC_BinarySemaphore, RXC_BinarySemaphore1, RXC2_BinarySemaphore;
static SemaphoreHandle_t TBE_BinarySemaphore, TBE_BinarySemaphore1, TBE_BinarySemaphore2;
static SemaphoreHandle_t LED_INT_BinarySemaphore;

static QueueHandle_t Data_Queue, Data_Queue1, red_kanal2;
static QueueHandle_t kalibracija_L_min, kalibracija_L_max;
static QueueHandle_t kalibracija_D_min, kalibracija_D_max;
static QueueHandle_t sistem_upaljen, rel_podatak, displej1, displej2;

static const uint8_t hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
static const uint8_t crtica = 0x40;

// Globalne varijable
static float_t g_mmL = 0.0f, g_mmD = 0.0f;
static float_t g_kalL = 0.0f, g_kalD = 0.0f;
static uint8_t g_aktivan = 0U;

// --- INTERRUPT HANDLERS ---
static uint32_t prvProcessTBEInterrupt(void) {
    BaseType_t xHigherPTW = pdFALSE;
    if (get_TBE_status(COM_CH_0) != 0) { xSemaphoreGiveFromISR(TBE_BinarySemaphore, &xHigherPTW); }
    if (get_TBE_status(COM_CH_1) != 0) { xSemaphoreGiveFromISR(TBE_BinarySemaphore1, &xHigherPTW); }
    if (get_TBE_status(COM_CH_2) != 0) { xSemaphoreGiveFromISR(TBE_BinarySemaphore2, &xHigherPTW); }
    portYIELD_FROM_ISR(xHigherPTW);
    return 0;
}

static uint32_t prvProcessRXCInterrupt(void) {
    BaseType_t xHigherPTW = pdFALSE;
    if (get_RXC_status(COM_CH_0) != 0) { xSemaphoreGiveFromISR(RXC_BinarySemaphore, &xHigherPTW); }
    if (get_RXC_status(COM_CH_1) != 0) { xSemaphoreGiveFromISR(RXC_BinarySemaphore1, &xHigherPTW); }
    if (get_RXC_status(COM_CH_2) != 0) { xSemaphoreGiveFromISR(RXC2_BinarySemaphore, &xHigherPTW); }
    portYIELD_FROM_ISR(xHigherPTW);
    return 0;
}

static uint32_t OnLED_ChangeInterrupt(void) {
    BaseType_t xHigherPTW = pdFALSE;
    xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW);
    portYIELD_FROM_ISR(xHigherPTW);
    return 0;
}

// Funkcija za slanje na Kanal 2
void posalji_na_pc(const char* str) {
    uint16_t i = 0U;
    while (str[i] != '\0') {
        if (get_TBE_status(COM_CH_2) == 0) {
            xSemaphoreTake(TBE_BinarySemaphore2, portMAX_DELAY);
        }
        send_serial_character(COM_CH_2, (uint8_t)str[i]);
        vTaskDelay(pdMS_TO_TICKS(40));
        i++;
    }
    xSemaphoreTake(TBE_BinarySemaphore2, pdMS_TO_TICKS(10));
    send_serial_character(COM_CH_2, 13U); // CR
    vTaskDelay(pdMS_TO_TICKS(40));
    send_serial_character(COM_CH_2, 10U); // LF
    vTaskDelay(pdMS_TO_TICKS(40));
}

void Dioda_Aktivacija_Task(void* pvParameters) {
    uint8_t flag = 1U;
    for (;;) {
        xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
        printf("SISTEM AKTIVIRAN KLIKOM NA DIODU\n");
        g_aktivan = 1U;
        xQueueSend(sistem_upaljen, &flag, 0U);
    }
}

void SerialReceive_Task(void* pvParameters) {
    uint8_t cc1 = 0U; uint8_t r_buffer1[R_BUF_SIZE]; uint8_t r_p1 = 0U;
    float_t broj = 0.0f; memset(r_buffer1, 0, R_BUF_SIZE);
    for (;;) {
        xSemaphoreTake(RXC_BinarySemaphore, portMAX_DELAY);
        if (get_serial_character(COM_CH_0, &cc1) == 0) {
            if (cc1 == 0x0dU) {
                r_buffer1[r_p1] = '\0'; broj = (float_t)atof((const char*)r_buffer1);
                g_mmL = broj;
                xQueueSend(Data_Queue, &broj, 0U); r_p1 = 0U; memset(r_buffer1, 0, R_BUF_SIZE);
            }
            else if (r_p1 < (uint8_t)(R_BUF_SIZE - 1U)) {
                r_buffer1[r_p1++] = cc1; 
            }
            else { /* Buffer je napunjen */ }
        }
    }
}

void SerialReceive_Task1(void* pvParameters) {
    uint8_t cc2 = 0U; uint8_t r_buffer2[R_BUF_SIZE]; uint8_t r_p2 = 0U;
    float_t broj1 = 0.0f; memset(r_buffer2, 0, R_BUF_SIZE);
    for (;;) {
        xSemaphoreTake(RXC_BinarySemaphore1, portMAX_DELAY);
        if (get_serial_character(COM_CH_1, &cc2) == 0) {
            if (cc2 == 0x0dU) {
                r_buffer2[r_p2] = '\0'; broj1 = (float_t)atof((const char*)r_buffer2);
                g_mmD = broj1;
                xQueueSend(Data_Queue1, &broj1, 0U); r_p2 = 0U; memset(r_buffer2, 0, R_BUF_SIZE);
            }
            else if (r_p2 < (uint8_t)(R_BUF_SIZE - 1U)) { 
                r_buffer2[r_p2++] = cc2; 
            }
            else { /* Buffer je napunjen */ }
        }
    }
}

void SerialReceive_Task2(void* pvParameters) {
    uint8_t cc = 0U; uint8_t rec[REC_BUF_50]; uint8_t r_point = 0U; memset(rec, 0, REC_BUF_50);
    for (;;) {
        xSemaphoreTake(RXC2_BinarySemaphore, portMAX_DELAY);
        if (get_serial_character(COM_CH_2, &cc) == 0) {
            if (cc == 0x0dU) {
                rec[r_point] = '\0';
                printf("Kanal 2 primio: %s\n", rec);
                xQueueSend(red_kanal2, &rec, 0U);
                r_point = 0U; memset(rec, 0, REC_BUF_50);
            }
            else if (r_point < (uint8_t)(REC_BUF_50 - 1U)) {
                rec[r_point++] = cc; 
            }
            else { /* Buffer je napunjen */ }
        }
    }
}

void Kalibracija_kanal(void* pvParameters) {
    uint8_t prijem_rec[REC_BUF_50] = { 0 }; int32_t val = 0; uint8_t flag = 0U;
    for (;;) {
        xQueueReceive(red_kanal2, prijem_rec, portMAX_DELAY);
        if (strcmp((const char*)&prijem_rec[0], "REVERSE") == 0) {
            flag = 1U; g_aktivan = 1U; printf("SISTEM AKTIVAN\n"); xQueueSend(sistem_upaljen, &flag, 0U);
        }
        else if (strcmp((const char*)prijem_rec, "PARK") == 0 ||
            strcmp((const char*)prijem_rec, "DRIVE") == 0 ||
            strcmp((const char*)prijem_rec, "NEUTRAL") == 0) {
            flag = 0U; g_aktivan = 0U; printf("SISTEM UGASEN\n");
            xQueueSend(sistem_upaljen, &flag, 0U);
            set_LED_BAR(1, 0x00); set_LED_BAR(2, 0x00);
            for (uint8_t i = 0U; i < 7U; i++) { select_7seg_digit(i); set_7seg_digit(0x00); }
        }
        else { /* Prazan else za sigurnost - MISRA */ }

        if (strstr((const char*)prijem_rec, "LIJEVI") != NULL) {
            if (strstr((const char*)prijem_rec, "_0%") != NULL) {
                if (sscanf((const char*)prijem_rec, "KALIBRACIJA_LIJEVI_%dmm_0%%", &val) == 1) {
                    printf("MINIMUM LIJEVI %d\n", val); xQueueSend(kalibracija_L_min, &val, 0U);
                }
            }
            else if (strstr((const char*)prijem_rec, "_100%") != NULL) {
                if (sscanf((const char*)prijem_rec, "KALIBRACIJA_LIJEVI_%dmm_100%%", &val) == 1) {
                    printf("MAKSIMUM LIJEVI %d\n", val); xQueueSend(kalibracija_L_max, &val, 0U);
                }
            }
            else { /* Prazan else za sigurnost - MISRA */ }
        }
        else if (strstr((const char*)prijem_rec, "DESNI") != NULL) {
            if (strstr((const char*)prijem_rec, "_0%") != NULL) {
                if (sscanf((const char*)prijem_rec, "KALIBRACIJA_DESNI_%dmm_0%%", &val) == 1) {
                    printf("MINIMUM DESNI %d\n", val); xQueueSend(kalibracija_D_min, &val, 0U);
                }
            }
            else if (strstr((const char*)prijem_rec, "_100%") != NULL) {
                if (sscanf((const char*)prijem_rec, "KALIBRACIJA_DESNI_%dmm_100%%", &val) == 1) {
                    printf("MAKSIMUM DESNI %d\n", val); xQueueSend(kalibracija_D_max, &val, 0U);
                }
            }
            else { /* Prazan else za sigurnost - MISRA */ }
        }
        else { /* Prazan else za sigurnost - MISRA */ }
    }
}

void racunaje_task(void* pvParameters) {
    float_t brL = 0.0f, brD = 0.0f;
    int32_t minL = 200, maxL = 1000, minD = 200, maxD = 1000;
    float_t kalL = 0.0f, kalD = 0.0f;
    uint8_t flag_sistem = 0U;
    for (;;) {
        xQueueReceive(kalibracija_L_min, &minL, 0);
        xQueueReceive(kalibracija_L_max, &maxL, 0);
        xQueueReceive(kalibracija_D_min, &minD, 0);
        xQueueReceive(kalibracija_D_max, &maxD, 0);
        xQueueReceive(sistem_upaljen, &flag_sistem, 0);
        if (xQueueReceive(Data_Queue, &brL, portMAX_DELAY) == pdPASS) {
            if (xQueueReceive(Data_Queue1, &brD, portMAX_DELAY) == pdPASS) {
                kalL = ((brL - (float_t)minL) / (float_t)(maxL - minL)) * 100.0f;
                kalD = ((brD - (float_t)minD) / (float_t)(maxD - minD)) * 100.0f;
                if (kalL < 0.0f) {
                    kalL = 0.0f; 
                }
                if (kalD < 0.0f) {
                    kalD = 0.0f; 
                }
                g_kalL = kalL; g_kalD = kalD;
                printf("Lijevi: %d%%, Desni: %d%%\n", (int32_t)kalL, (int32_t)kalD);

                // NAPOMENA: Za finalnu verziju bi bilo bolje definisati 
                // granične vrijednosti (npr. 100, 50, 20) kao makroe (#define), 
                // ali sam ih ostavila ovako radi lakšeg testiranja simulatora.
                
                if (kalL < 20.0f || kalD < 20.0f) {
                    printf("ZONA: KONTAKT_DETEKCIJA\n"); 
                }
                else if (kalL > 100.0f && kalD > 100.0f) {
                    printf("ZONA: NEMA_DETEKCIJE\n"); 
                }
                else { /* Sigruna zona */ }

                xQueueSend(displej1, &kalL, 0U); xQueueSend(displej2, &kalD, 0U);
                float_t rel = (kalL < kalD) ? kalL : kalD;
                xQueueSend(rel_podatak, &rel, 0U);
            }
        }
    }
}

void PC_Reporting_Task(void* pvParameters) {
    char poruka[REC_BUF_50];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (g_aktivan != 0U) {
            sprintf(poruka, "LIJEVI: %dmm", (int32_t)g_mmL);
            posalji_na_pc(poruka);
            vTaskDelay(pdMS_TO_TICKS(100));

            sprintf(poruka, "DESNI: %dmm", (int32_t)g_mmD);
            posalji_na_pc(poruka);
            vTaskDelay(pdMS_TO_TICKS(100));

            if (g_kalL < 20.0f || g_kalD < 20.0f) {
                posalji_na_pc("ZONA: KONTAKT_DETEKCIJA");
            }
            else if (g_kalL > 100.0f && g_kalD > 100.0f) {
                posalji_na_pc("ZONA: NEMA_DETEKCIJE");
            }
            else {
                posalji_na_pc("ZONA: OK");
            }
            posalji_na_pc("----------------");
        }
    }
}

void LED_bar(void* pvParameters) {
    float_t kal = -1.0f; uint8_t flag = 0U;
    for (;;) {
        uint8_t prethodni_flag = flag;
        xQueueReceive(sistem_upaljen, &flag, 0);
        if (prethodni_flag == 0U && flag == 1U) { kal = -1.0f; }
        xQueueReceive(rel_podatak, &kal, 10);
        if (flag == 1U) {
            set_LED_BAR(1, 0x01);
            if (kal < 0.0f) {
                set_LED_BAR(2, 0x00); 
            }
            else if (kal <= 20.0f) {
                set_LED_BAR(2, 0xFF); 
            }
            else if (kal >= 100.0f) {
                set_LED_BAR(2, 0x00); 
            }
            else {
                int32_t broj_dioda = (int32_t)((100.0f - kal) / 12.5f);
                uint8_t maska = 0U;
                for (int32_t i = 0; i < broj_dioda; i++) {
                    maska |= (uint8_t)(1 << i); 
                }
                set_LED_BAR(2, maska);
            }
        }
        else {
            set_LED_BAR(1, 0x00); set_LED_BAR(2, 0x00); 
        }
    }
}

void LCD_Displej(void* pvParams) {
    float_t k1 = 0.0f, k2 = 0.0f; int32_t p;
    for (;;) {
        if (xQueueReceive(displej1, &k1, 100) == pdPASS) {
            if (k1 <= 20.0f) { select_7seg_digit(0); set_7seg_digit(crtica); select_7seg_digit(1); set_7seg_digit(crtica); select_7seg_digit(2); set_7seg_digit(crtica); }
            else {
                p = (int32_t)k1;
                select_7seg_digit(0); set_7seg_digit(p >= 100 ? hexnum[p / 100] : 0x00);
                select_7seg_digit(1); set_7seg_digit(hexnum[(p / 10) % 10]);
                select_7seg_digit(2); set_7seg_digit(hexnum[p % 10]);
            }
        }
        select_7seg_digit(3); set_7seg_digit(0x40);
        if (xQueueReceive(displej2, &k2, 100) == pdPASS) {
            if (k2 <= 20.0f) {
                select_7seg_digit(4); set_7seg_digit(crtica); select_7seg_digit(5); set_7seg_digit(crtica); select_7seg_digit(6); set_7seg_digit(crtica); 
            }
            else {
                p = (int32_t)k2;
                select_7seg_digit(4); set_7seg_digit(p >= 100 ? hexnum[p / 100] : 0x00);
                select_7seg_digit(5); set_7seg_digit(hexnum[(p / 10) % 10]);
                select_7seg_digit(6); set_7seg_digit(hexnum[p % 10]);
            }
        }
    }
}

void main_demo(void) {
    init_7seg_comm(); init_LED_comm();
    init_serial_uplink(COM_CH_0); init_serial_downlink(COM_CH_0);
    init_serial_uplink(COM_CH_1); init_serial_downlink(COM_CH_1);
    init_serial_uplink(COM_CH_2); init_serial_downlink(COM_CH_2);

    set_LED_BAR(1, 0x00); set_LED_BAR(2, 0x00);
    for (uint8_t i = 0U; i < 7U; i++) {
        select_7seg_digit(i); set_7seg_digit(0x00); 
    }

    vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);
    vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);
    vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);

    RXC_BinarySemaphore = xSemaphoreCreateBinary();
    RXC_BinarySemaphore1 = xSemaphoreCreateBinary();
    RXC2_BinarySemaphore = xSemaphoreCreateBinary();
    TBE_BinarySemaphore = xSemaphoreCreateBinary();
    TBE_BinarySemaphore1 = xSemaphoreCreateBinary();
    TBE_BinarySemaphore2 = xSemaphoreCreateBinary();
    LED_INT_BinarySemaphore = xSemaphoreCreateBinary();

    xSemaphoreGive(TBE_BinarySemaphore2);

    Data_Queue = xQueueCreate(5, sizeof(float_t));
    Data_Queue1 = xQueueCreate(5, sizeof(float_t));
    red_kanal2 = xQueueCreate(5, sizeof(uint8_t[REC_BUF_50]));
    kalibracija_L_min = xQueueCreate(1, sizeof(int32_t));
    kalibracija_L_max = xQueueCreate(1, sizeof(int32_t));
    kalibracija_D_min = xQueueCreate(1, sizeof(int32_t));
    kalibracija_D_max = xQueueCreate(1, sizeof(int32_t));
    sistem_upaljen = xQueueCreate(1, sizeof(uint8_t));
    rel_podatak = xQueueCreate(1, sizeof(float_t));
    displej1 = xQueueCreate(1, sizeof(float_t));
    displej2 = xQueueCreate(1, sizeof(float_t));

    // Razmotriti da li task za očitavanje senzora treba da ima 
    // veći prioritet od taska za ispis na terminal, kako ne bismo 
    // gubili podatke u realnom vremenu pri većim brzinama.
    
    xTaskCreate(SerialReceive_Task, "SRx0", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);
    xTaskCreate(SerialReceive_Task1, "SRx1", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);
    xTaskCreate(SerialReceive_Task2, "SRx2", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);
    xTaskCreate(Kalibracija_kanal, "Kali", configMINIMAL_STACK_SIZE, NULL, OBRADA_PRI, NULL);
    xTaskCreate(racunaje_task, "Rac", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI_kanali, NULL);
    xTaskCreate(LED_bar, "LED", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);
    xTaskCreate(LCD_Displej, "Disp", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);
    xTaskCreate(Dioda_Aktivacija_Task, "Dio", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);
    xTaskCreate(PC_Reporting_Task, "PC", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);

    vTaskStartScheduler();
    while (1) { ; }
}
