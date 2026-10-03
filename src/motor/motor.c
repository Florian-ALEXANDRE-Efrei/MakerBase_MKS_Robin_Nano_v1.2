#include "motor.h"

#include <stdbool.h>
#include "stm32f1xx_hal.h"

// Si le sens du moteur est inversé par rapport à ce que tu attends, mets 1 ici.
#define MOTOR_Z_DIR_INVERT 0

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} pin_t;

struct motor_pins_t
{
    pin_t enable; // actif à l'état bas (cas des drivers A4988 / DRV8825 / TMC)
    pin_t step;
    pin_t dir;
    bool dir_invert;
};

static const struct motor_pins_t motor_pins[MOTOR_COUNT] = {
    [MOTOR_Z] = {
        .enable = {GPIOB, GPIO_PIN_8},
        .step = {GPIOB, GPIO_PIN_5},
        .dir = {GPIOB, GPIO_PIN_4},
        .dir_invert = MOTOR_Z_DIR_INVERT,
    },
};

// État de chaque moteur, partagé entre le programme principal et l'interruption TIM6
struct motor_state_t
{
    volatile uint32_t remaining; // pas restant à émettre
    volatile int32_t position;
    volatile uint32_t speed_q16; // vitesse courante en pas/s, format 16.16
    volatile uint32_t acc;       // accumulateur de phase (génère les pas à la bonne cadence)
    volatile uint8_t pulse_high; // 1 pendant l'impulsion STEP
    uint32_t target;             // vitesse de croisière, pas/s
    int32_t dir;                 // +1 ou -1
    bool enabled;
};

static struct motor_state_t state[MOTOR_COUNT];

#define ACCEL_Q16 ((uint32_t)(((uint64_t)MOTOR_ACCEL << 16) / MOTOR_TICK_HZ)) // variation de vitesse par tick

static void gpio_clock_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA)
        __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB)
        __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC)
        __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD)
        __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE)
        __HAL_RCC_GPIOE_CLK_ENABLE();
}

static void pin_output(pin_t p)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = p.pin;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(p.port, &g);
}

static inline void pin_set(pin_t p) { p.port->BSRR = p.pin; }
static inline void pin_reset(pin_t p) { p.port->BSRR = (uint32_t)p.pin << 16; }

void motor_init(void)
{
    // PB4 (DIR du moteur Z) est NJTRST au reset : on libère PB3, PB4 et PA15, le SWD reste actif
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    for (int i = 0; i < MOTOR_COUNT; i++)
    {
        const struct motor_pins_t *p = &motor_pins[i];
        gpio_clock_enable(p->enable.port);
        gpio_clock_enable(p->step.port);
        gpio_clock_enable(p->dir.port);

        pin_set(p->enable); // niveau « désactivé » écrit AVANT de passer la broche en sortie,
        pin_reset(p->step); // pour que le driver ne soit jamais activé par erreur
        pin_reset(p->dir);
        pin_output(p->enable);
        pin_output(p->step);
        pin_output(p->dir);
    }

    // TIM6 : interruption à MOTOR_TICK_HZ. Démarré seulement pendant un mouvement.
    __HAL_RCC_TIM6_CLK_ENABLE();
    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
        tim_clk *= 2; // les timers tournent à 2 x PCLK1 si APB1 est divisé
    TIM6->CR1 = 0;
    TIM6->PSC = 0;
    TIM6->ARR = tim_clk / MOTOR_TICK_HZ - 1;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0;
    TIM6->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM6_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM6_IRQn);
}

void motor_enable(motor_id_t m, int enable)
{
    if (m >= MOTOR_COUNT)
        return;
    if (!enable)
    {
        motor_stop(m);
        pin_set(motor_pins[m].enable);
    }
    else
    {
        pin_reset(motor_pins[m].enable);
    }
    state[m].enabled = enable != 0;
}

int motor_move(motor_id_t m, int32_t steps, uint32_t speed)
{
    if (m >= MOTOR_COUNT)
        return MOTOR_ERR_ARG;
    struct motor_state_t *s = &state[m];
    if (!s->enabled)
        return MOTOR_ERR_DISABLED;
    if (motor_is_moving(m))
        return MOTOR_ERR_BUSY;
    if (steps == 0)
        return MOTOR_OK;

    if (speed > MOTOR_MAX_SPEED)
        speed = MOTOR_MAX_SPEED;
    if (speed < MOTOR_START_SPEED)
        speed = MOTOR_START_SPEED;

    const struct motor_pins_t *p = &motor_pins[m];
    bool forward = (steps > 0) != p->dir_invert;
    if (forward)
        pin_set(p->dir);
    else
        pin_reset(p->dir);

    uint32_t count = steps > 0 ? (uint32_t)steps : (uint32_t)(-(int64_t)steps);

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s->dir = steps > 0 ? 1 : -1;
    s->target = speed;
    s->speed_q16 = MOTOR_START_SPEED << 16;
    s->acc = 0;
    s->remaining = count; // en dernier : c'est lui qui « arme » le mouvement
    TIM6->CR1 |= TIM_CR1_CEN;
    __set_PRIMASK(primask);
    return MOTOR_OK;
}

int motor_is_moving(motor_id_t m)
{
    if (m >= MOTOR_COUNT)
        return 0;
    return state[m].remaining > 0 || state[m].pulse_high;
}

void motor_wait(motor_id_t m)
{
    while (motor_is_moving(m))
    {
    }
}

void motor_stop(motor_id_t m)
{
    if (m >= MOTOR_COUNT)
        return;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    state[m].remaining = 0; // l'impulsion en cours est terminée par l'interruption
    __set_PRIMASK(primask);
}

int32_t motor_position(motor_id_t m)
{
    return m < MOTOR_COUNT ? state[m].position : 0;
}

// Appelée MOTOR_TICK_HZ fois par seconde pendant un mouvement
void TIM6_IRQHandler(void)
{
    TIM6->SR = 0;
    bool active = false;

    for (int i = 0; i < MOTOR_COUNT; i++)
    {
        struct motor_state_t *s = &state[i];
        const struct motor_pins_t *p = &motor_pins[i];

        bool lowered = false;
        if (s->pulse_high)
        { // fin de l'impulsion : STEP reste bas au moins un tick
            pin_reset(p->step);
            s->pulse_high = 0;
            lowered = true;
        }
        if (s->remaining == 0)
            continue;
        active = true;

        // Rampe : on freine dès que la distance restante n'est plus qu'à peine suffisante (v² = 2·a·d)
        uint32_t v = s->speed_q16 >> 16;
        if ((uint64_t)v * v >= 2ull * MOTOR_ACCEL * s->remaining)
        {
            s->speed_q16 = s->speed_q16 > (MOTOR_START_SPEED << 16) + ACCEL_Q16
                               ? s->speed_q16 - ACCEL_Q16
                               : MOTOR_START_SPEED << 16;
        }
        else if (v < s->target)
        {
            s->speed_q16 = s->speed_q16 + ACCEL_Q16 < (s->target << 16)
                               ? s->speed_q16 + ACCEL_Q16
                               : s->target << 16;
        }

        s->acc += s->speed_q16 >> 16; // la phase avance à chaque tick : un pas chaque fois qu'elle passe MOTOR_TICK_HZ
        if (s->acc >= MOTOR_TICK_HZ && !lowered)
        { // pas de nouveau pas au tick où STEP vient de retomber
            s->acc -= MOTOR_TICK_HZ;
            pin_set(p->step);
            s->pulse_high = 1;
            s->remaining--;
            s->position += s->dir;
        }
    }

    if (!active)
        TIM6->CR1 &= ~TIM_CR1_CEN; // plus rien à faire : le timer s'arrête
}
