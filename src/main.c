#include "stm32f1xx_hal.h"

void SysTick_Handler(void) { HAL_IncTick(); }   // indispensable : pas de stm32f1xx_it.c

static void SystemClock_Config(void)
{
    HAL_RCC_DeInit();                            // quitte la PLL laissée par le bootloader

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL          = RCC_PLL_MUL16;    // 4 MHz x 16 = 64 MHz
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;          // APB1 <= 36 MHz
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

int main(void)
{
    // Marqueur : D7 allumée = main() atteint
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 8)) | (0x2u << 8);   // PB10 sortie push-pull
    GPIOB->BRR = GPIO_PIN_10;                                  // PB10 = 0 -> D7 allumée

    // Reprise après le bootloader MKS
    SCB->VTOR = 0x08007000;                      // notre table des vecteurs
    SysTick->CTRL = 0;                           // tick du bootloader arrêté
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;              // IRQ du bootloader désactivées
        NVIC->ICPR[i] = 0xFFFFFFFF;              // et effacées
    }
    __enable_irq();                              // au cas où il les aurait coupées

    HAL_Init();
    SystemClock_Config();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_10;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);

    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_10);  // LED TX
        HAL_Delay(500);
    }
}