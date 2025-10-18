#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include "esp_err.h"

/**
 * @brief Inicia el bot de Telegram en una tarea independiente.
 * 
 * Esta función crea la tarea que periódicamente consulta la API
 * de Telegram y responde a los comandos recibidos.
 */
void telegram_bot_start(void);

/**
 * @brief Devuelve el retardo actual configurado mediante /speed.
 */
int telegram_get_delay(void);

#endif // TELEGRAM_BOT_H

