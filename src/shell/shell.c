#include "shell.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "stm32f1xx_hal.h"

#define SHELL_BUFFER_SIZE 256
#define SHELL_TX_TIMEOUT_MS 100

#define SHELL_PROMPT "> "
#define SHELL_LINE_SIZE 80
#define SHELL_MAX_ARGS 8
#define SHELL_MAX_CMDS 16
#define SHELL_RX_SIZE 128 // puissance de 2

static UART_HandleTypeDef huart3;

// Octets reçus, remplis par l'interruption USART3 et vidés par shell_poll()
static volatile uint8_t rx_buf[SHELL_RX_SIZE];
static volatile uint16_t rx_head; // écrit par l'interruption
static volatile uint16_t rx_tail; // écrit par shell_poll()

typedef struct
{
    const char *name;
    const char *help;
    shell_cmd_fn fn;
} shell_cmd_t;

static shell_cmd_t cmds[SHELL_MAX_CMDS];
static int cmd_count;

// Appelée par HAL_UART_Init() : horloges et broches (PB10 = TX, PB11 = RX)
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

    gpio.Pin  = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP; // ligne au repos à l'état haut même si le CH340 est débranché
    HAL_GPIO_Init(GPIOB, &gpio);
}

void USART3_IRQHandler(void)
{
    uint32_t sr = USART3->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE))
    {
        uint8_t c = (uint8_t)USART3->DR; // lire DR efface aussi RXNE / ORE
        uint16_t next = (rx_head + 1) & (SHELL_RX_SIZE - 1);
        if (next != rx_tail) // buffer plein : l'octet est perdu
        {
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

void shell_init(uint32_t baudrate)
{
    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = baudrate;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart3);

    HAL_NVIC_SetPriority(USART3_IRQn, 2, 0); // moins prioritaire que TIM6 (moteurs)
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
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

int shell_register(const char *name, const char *help, shell_cmd_fn fn)
{
    if (cmd_count >= SHELL_MAX_CMDS)
        return -1;
    cmds[cmd_count++] = (shell_cmd_t){name, help, fn};
    return 0;
}

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < cmd_count; i++)
        shell_printf("  %-10s %s\n", cmds[i].name, cmds[i].help);
    return 0;
}

static void shell_execute(char *line)
{
    char *argv[SHELL_MAX_ARGS];
    int argc = 0;

    // Découpe la ligne en mots séparés par des espaces (modifie le buffer)
    for (char *p = line; *p;)
    {
        while (*p == ' ' || *p == '\t')
            *p++ = '\0';
        if (!*p)
            break;
        if (argc == SHELL_MAX_ARGS)
        {
            shell_printf("erreur: trop d'arguments (max %d)\n", SHELL_MAX_ARGS);
            return;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    if (argc == 0)
        return;

    for (int i = 0; i < cmd_count; i++)
    {
        if (strcmp(argv[0], cmds[i].name) == 0)
        {
            int ret = cmds[i].fn(argc, argv);
            if (ret != 0)
                shell_printf("erreur: %d\n", ret);
            return;
        }
    }
    shell_printf("commande inconnue: %s (essayer 'help')\n", argv[0]);
}

void shell_poll(void)
{
    static char line[SHELL_LINE_SIZE];
    static uint16_t len;
    static uint8_t overflow;
    static uint8_t last_was_cr;
    static uint8_t started;

    if (!started)
    {
        started = 1;
        if (cmd_count < SHELL_MAX_CMDS)
            shell_register("help", "liste les commandes", cmd_help);
        shell_printf(SHELL_PROMPT);
    }

    while (rx_tail != rx_head)
    {
        char c = (char)rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & (SHELL_RX_SIZE - 1);

        if (c == '\n' && last_was_cr) // fin de ligne "\r\n" : déjà traitée au '\r'
        {
            last_was_cr = 0;
            continue;
        }
        last_was_cr = (c == '\r');

        if (c == '\r' || c == '\n')
        {
            shell_printf("\n");
            if (overflow)
                shell_printf("erreur: ligne trop longue (max %d)\n", SHELL_LINE_SIZE - 1);
            else
            {
                line[len] = '\0';
                shell_execute(line);
            }
            len = 0;
            overflow = 0;
            shell_printf(SHELL_PROMPT);
        }
        else if (c == '\b' || c == 0x7F) // backspace
        {
            if (len > 0 && !overflow)
            {
                len--;
                shell_write("\b \b", 3);
            }
        }
        else if (c >= ' ' && c < 0x7F)
        {
            if (len < SHELL_LINE_SIZE - 1)
            {
                line[len++] = c;
                shell_write(&c, 1); // écho local
            }
            else
                overflow = 1;
        }
    }
}
