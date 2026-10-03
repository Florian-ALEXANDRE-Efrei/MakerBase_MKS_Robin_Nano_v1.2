#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include "stm32f1xx_hal.h"

#define SHELL_BUFFER_SIZE 256
#define SHELL_TX_TIMEOUT_MS 100

static UART_HandleTypeDef huart3;

// Appelée par HAL_UART_Init() : horloges et broches (PB10 = TX en fonction alternative)
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3)
        return;

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_10;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void shell_init(uint32_t baudrate)
{
    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = baudrate;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3);
}

static int shell_write(const char *data, uint16_t len)
{
    if (len == 0)
        return 0;
    return HAL_UART_Transmit(&huart3, (uint8_t *)data, len, SHELL_TX_TIMEOUT_MS) == HAL_OK ? 0 : -1;
}

int shell_printf(const char *fmt, ...)
{
    char buf[SHELL_BUFFER_SIZE];

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    if (n < 0)
        return n;
    if (n >= (int)sizeof buf)
        n = sizeof buf - 1;   // message tronqué

    // Envoie le texte par morceaux, en insérant '\r' avant chaque '\n'
    int start = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            if (shell_write(&buf[start], (uint16_t)(i - start)) < 0 || shell_write("\r\n", 2) < 0)
                return -1;
            start = i + 1;
        }
    }
    if (shell_write(&buf[start], (uint16_t)(n - start)) < 0)
        return -1;
    return n;
}
