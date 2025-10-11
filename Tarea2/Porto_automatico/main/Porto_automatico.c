#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

#define ESTADO_INIT_CONFIG 0
#define ESTADO_CERRANDO 1
#define ESTADO_ABRIENDO 2
#define ESTADO_ABIERTO 3
#define ESTADO_CERRADO 4
#define ESTADO_ERROR 5
#define ESTADO_PARADO 6

#define ERR_SYS_OK 0
#define ERR_SYS_DLSA 1 //los dos limit switch estan activo
#define ERR_SYS_RT 2 //superado el tiempo de movimiento del motor

#define TIMER_CA_3min 600
#define TIMER_MOVIMIENTO_MAX 400 // tiempo máximo para abrir/cerrar (20 segundos)

#define LSA_ACTIVO 0
#define LSA_NO_ACTIVO 1
#define LSC_ACTIVO 1
#define LSC_NO_ACTIVO 0

#define MA_ON 1
#define MA_OFF 0
#define MC_ON 1
#define MC_OFF 0

#define PP_ACTIVO 1
#define PP_NO_ACTIVO 0

#define FTC_ACTIVO 1
#define FTC_NO_ACTIVO 0

#define LAMPARA_OFF 0
#define LAMPARA_ERR 1
#define LAMPARA_MOVIMIENTO 3
#define LAMPARA_PARADA 4

// Variables globales
int EstadoActual = ESTADO_INIT_CONFIG;
int EstadoSiguiente = ESTADO_INIT_CONFIG;
int EstadoAnterior = ESTADO_INIT_CONFIG;
int Error_Code = ERR_SYS_OK;
int cnt_tiempo_ca = 0;
int cnt_tiempo_movimiento = 0;

// Prototipos de funciones
int Func_ESTADO_INIT_CONFIG(void);
int Func_ESTADO_CERRANDO(void);
int Func_ESTADO_CERRADO(void);
int Func_ESTADO_ABIERTO(void);
int Func_ESTADO_ABRIENDO(void);
int Func_ESTADO_PARADO(void);
int Func_ESTADO_ERROR(void);
void SetupIO(void);
void SetupTimer(void);
void SetupVariables(void);
void TimerCallBack(void);

struct IO
{
    unsigned int lsa:1;
    unsigned int lsc:1;
    unsigned int ma:1;
    unsigned int mc:1;
    unsigned int ca:1;
    unsigned int cc:1;
    unsigned int pp:1;
    unsigned int ftc:1;
    unsigned int lamp:3;
} io;

void SetupIO(void)
{
    // Inicializar todas las entradas/salidas
    io.lsa = LSA_NO_ACTIVO;
    io.lsc = LSC_NO_ACTIVO;
    io.ma = MA_OFF;
    io.mc = MC_OFF;
    io.ca = 0;
    io.cc = 0;
    io.pp = PP_NO_ACTIVO;
    io.ftc = FTC_NO_ACTIVO;
    io.lamp = LAMPARA_OFF;
}

void SetupTimer(void)
{
    cnt_tiempo_ca = 0;
    cnt_tiempo_movimiento = 0;
}

void SetupVariables(void)
{
    EstadoActual = ESTADO_INIT_CONFIG;
    EstadoSiguiente = ESTADO_INIT_CONFIG;
    EstadoAnterior = ESTADO_INIT_CONFIG;
    Error_Code = ERR_SYS_OK;
}

void TimerCallBack(void)
{
    cnt_tiempo_ca++; // contador multiplo de 50ms
    cnt_tiempo_movimiento++; // contador para tiempo de movimiento
    
    // Simular lectura de sensores (en un sistema real aquí irían las lecturas GPIO)
    if(io.lsa == LSA_ACTIVO) {
        // Simular que el sensor está activo
    }
}

int main()
{
    SetupIO();
    SetupTimer();
    SetupVariables();
    
    while(1) {
        // Ejecutar callback del timer (en sistema real esto sería interrupción)
        TimerCallBack();
        
        if(EstadoSiguiente == ESTADO_INIT_CONFIG) {
            EstadoSiguiente = Func_ESTADO_INIT_CONFIG();
        }
        else if(EstadoSiguiente == ESTADO_ABIERTO) {
            EstadoSiguiente = Func_ESTADO_ABIERTO();
        }
        else if(EstadoSiguiente == ESTADO_ABRIENDO) {
            EstadoSiguiente = Func_ESTADO_ABRIENDO();
        }
        else if(EstadoSiguiente == ESTADO_CERRADO) {
            EstadoSiguiente = Func_ESTADO_CERRADO();
        }
        else if(EstadoSiguiente == ESTADO_CERRANDO) {
            EstadoSiguiente = Func_ESTADO_CERRANDO();
        }
        else if(EstadoSiguiente == ESTADO_ERROR) {
            EstadoSiguiente = Func_ESTADO_ERROR();
        }
        else if(EstadoSiguiente == ESTADO_PARADO) {
            EstadoSiguiente = Func_ESTADO_PARADO();
        }
        
        // Pequeña pausa para simular tiempo real
        // usleep(50000); // 50ms en sistema real
    }
    
    return 0;
}

int Func_ESTADO_INIT_CONFIG()
{
    // inicializacion del estado
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_INIT_CONFIG;
    
    io.lamp = LAMPARA_OFF;
    io.ma = MA_OFF;
    io.mc = MC_OFF;

    // Verificar estado de los sensores
    if((io.lsa == LSA_ACTIVO) && (io.lsc == LSC_ACTIVO)) {
        Error_Code = ERR_SYS_DLSA;
        return ESTADO_ERROR;
    }

    if(io.lsc == LSC_ACTIVO) {
        return ESTADO_CERRADO;
    }

    if(io.lsa == LSA_ACTIVO) {
        return ESTADO_ABIERTO;
    }

    // Si no hay sensores activos, asumir abierto
    return ESTADO_ABIERTO;
}

int Func_ESTADO_CERRADO()
{
    // inicializacion del estado
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRADO;
    io.mc = MC_OFF;
    io.ma = MA_OFF;
    io.lamp = LAMPARA_OFF;
    cnt_tiempo_ca = 0;

    while(1) {
        if(io.pp == PP_ACTIVO) {
            io.pp = PP_NO_ACTIVO;
            return ESTADO_ABRIENDO;
        }
        
        // Verificar si el sensor se desactiva (error)
        if(io.lsc != LSC_ACTIVO) {
            return ESTADO_INIT_CONFIG;
        }
    }
}

int Func_ESTADO_CERRANDO()
{
    // inicializacion del estado
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_CERRANDO;
    
    io.ma = MA_OFF;
    io.mc = MC_ON;  // Activar motor de cierre
    io.lamp = LAMPARA_MOVIMIENTO;
    cnt_tiempo_movimiento = 0;

    while(1) {
        // Verificar timeout de movimiento
        if(cnt_tiempo_movimiento > TIMER_MOVIMIENTO_MAX) {
            Error_Code = ERR_SYS_RT;
            return ESTADO_ERROR;
        }
        
        // Verificar si llegó al final de carrera cerrado
        if(io.lsc == LSC_ACTIVO) {
            io.mc = MC_OFF;
            return ESTADO_CERRADO;
        }
        
        // Verificar pulsador de parada
        if(io.pp == PP_ACTIVO) {
            io.mc = MC_OFF;
            io.pp = PP_NO_ACTIVO;
            return ESTADO_PARADO;
        }
        
        // Verificar obstáculos (sensor de seguridad)
        if(io.ca == 1) {
            io.mc = MC_OFF;
            return ESTADO_ABRIENDO; // Reversa por seguridad
        }
    }
}

int Func_ESTADO_ABRIENDO()
{
    // inicializacion del estado
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABRIENDO;
    
    io.mc = MC_OFF;
    io.ma = MA_ON;  // Activar motor de apertura
    io.lamp = LAMPARA_MOVIMIENTO;
    cnt_tiempo_movimiento = 0;

    while(1) {
        // Verificar timeout de movimiento
        if(cnt_tiempo_movimiento > TIMER_MOVIMIENTO_MAX) {
            Error_Code = ERR_SYS_RT;
            return ESTADO_ERROR;
        }
        
        // Verificar si llegó al final de carrera abierto
        if(io.lsa == LSA_ACTIVO) {
            io.ma = MA_OFF;
            return ESTADO_ABIERTO;
        }
        
        // Verificar pulsador de parada
        if(io.pp == PP_ACTIVO) {
            io.ma = MA_OFF;
            io.pp = PP_NO_ACTIVO;
            return ESTADO_PARADO;
        }
        
        // Verificar obstáculos (sensor de seguridad)
        if(io.ca == 1) {
            io.ma = MA_OFF;
            return ESTADO_CERRANDO; // Reversa por seguridad
        }
    }
}

int Func_ESTADO_ABIERTO()
{
    // inicializacion del estado
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ABIERTO;
    
    io.ma = MA_OFF;
    io.mc = MC_OFF;
    io.lamp = LAMPARA_OFF;
    
    if(io.ftc == FTC_ACTIVO) {
        cnt_tiempo_ca = 0;
    }

    while(1) {
        // Verificar timeout de 3 minutos
        if(cnt_tiempo_ca > TIMER_CA_3min) {
            return ESTADO_CERRANDO; // Cierre automático
        }
        
        // Verificar pulsador
        if(io.pp == PP_ACTIVO) {
            io.pp = PP_NO_ACTIVO;
            return ESTADO_CERRANDO;
        }
        
        // Verificar si el sensor se desactiva (error)
        if(io.lsa != LSA_ACTIVO) {
            return ESTADO_INIT_CONFIG;
        }
    }
}

int Func_ESTADO_ERROR()
{
    // inicializacion del estado
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_ERROR;
    io.ma = MA_OFF;
    io.mc = MC_OFF;

    while(1) {
        if(Error_Code == ERR_SYS_RT) {
            io.lamp = LAMPARA_ERR;
            // TODO enviar error por MQTT
            // En sistema real, aquí habría una forma de resetear
            return ESTADO_INIT_CONFIG; // Por ahora volvemos a inicializar
        }

        if(Error_Code == ERR_SYS_DLSA) {
            io.lamp = LAMPARA_ERR;
            // TODO enviar error por MQTT
            // Esperar a que se resuelva el conflicto de sensores
            if(!(io.lsa == LSA_ACTIVO && io.lsc == LSC_ACTIVO)) {
                return ESTADO_INIT_CONFIG;
            }
        }
    }
}

int Func_ESTADO_PARADO()
{
    // inicializacion del estado
    EstadoAnterior = EstadoActual;
    EstadoActual = ESTADO_PARADO;
    
    io.ma = MA_OFF;
    io.mc = MC_OFF;
    io.lamp = LAMPARA_PARADA;

    while(1) {
        // Pulsador para continuar movimiento
        if(io.pp == PP_ACTIVO) {
            io.pp = PP_NO_ACTIVO;
            
            // Determinar dirección basado en estado anterior
            if(EstadoAnterior == ESTADO_CERRANDO) {
                return ESTADO_CERRANDO;
            } else if(EstadoAnterior == ESTADO_ABRIENDO) {
                return ESTADO_ABRIENDO;
            } else {
                return ESTADO_INIT_CONFIG; // Por defecto
            }
        }
        
        // Timeout de parada - volver a estado inicial
        if(cnt_tiempo_ca > (TIMER_CA_3min / 2)) {
            return ESTADO_INIT_CONFIG;
        }
    }
}