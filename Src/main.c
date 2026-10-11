/**
  ******************************************************************************
  * @file    main.c
  * @brief   Лабораторная работа № 2. Формирование сигнала управления ESC
  *          по протоколу OneShot42 на канале TIM5_CH3 (вывод PA2)
  *          микроконтроллера STM32F401RE.
  *
  *          Вариант задания:
  *            - таймер/канал : TIM5_CH3 (PA2, AF2);
  *            - протокол ESC : OneShot42 (42 мкс - 0 %, 84 мкс - 100 %);
  *            - уровень газа : 0 %  ->  длительность импульса 42 мкс.
  *
  *          Параметры таймера (расчёт см. в отчёте):
  *            f(TIM5) = 84 МГц; PSC = 0; ARR = 10499 (период кадра 125 мкс,
  *            частота следования 8 кГц); CCR3 = 3528 (импульс 42 мкс).
  ******************************************************************************
  */

#include "main.h"

/* Тактовая частота счётчика TIM5 после предделителя, Гц ------------------- */
#define TIM5_CLK_HZ          16000000UL

/* Параметры протокола OneShot42, нс ---------------------------------------- */
#define OS42_PULSE_MIN_NS    42000UL   /* 42 мкс - газ 0 %   */
#define OS42_PULSE_MAX_NS    84000UL   /* 84 мкс - газ 100 % */

/* Период следования кадров: 125 мкс (8 кГц) -------------------------------- */
#define OS42_FRAME_NS        125000UL

/* Число тактов таймера, приходящееся на 1 мкс (84 такта) ------------------- */
#define TICKS_PER_US         (TIM5_CLK_HZ / 1000000UL)

/* Значения, загружаемые в регистры таймера --------------------------------- */
#define TIM5_PRESCALER       0U                                   /* PSC      */
#define TIM5_PERIOD          ((OS42_FRAME_NS * TICKS_PER_US) / 1000UL - 1U) /* ARR = 10499 */

/* Заданный вариантом уровень газа, % --------------------------------------- */
#define THROTTLE_PERCENT     0.0f

TIM_HandleTypeDef htim5;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM5_Init(void);
static void ESC_SetThrottle(float percent);

/**
  * @brief  Пересчёт уровня газа в содержимое регистра сравнения CCR3.
  * @param  percent - уровень газа в процентах (0...100).
  * @note   Длительность импульса OneShot42:
  *           t[мкс] = 42 + (84 - 42) * percent / 100;
  *         код регистра сравнения:
  *           CCR3 = t[мкс] * 84.
  */
static void ESC_SetThrottle(float percent)
{
  uint32_t pulse_ns;
  uint32_t ccr;

  /* Ограничение входного значения допустимым диапазоном */
  if (percent < 0.0f)   { percent = 0.0f;   }
  if (percent > 100.0f) { percent = 100.0f; }

  /* Линейная интерполяция длительности импульса между 42 и 84 мкс */
  pulse_ns = OS42_PULSE_MIN_NS +
             (uint32_t)((OS42_PULSE_MAX_NS - OS42_PULSE_MIN_NS) * percent / 100.0f);

  /* Перевод длительности в такты таймера: N = t[нс] * 84 / 1000 */
  ccr = (pulse_ns * TICKS_PER_US) / 1000UL;

  /* Запись в регистр сравнения 3-го канала (теневой регистр, с предзагрузкой) */
  __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_3, ccr);
}

/**
  * @brief  Точка входа в программу.
  */
int main(void)
{
  /* Инициализация HAL, настройка FLASH latency и прерывания SysTick */
  HAL_Init();

  /* Перевод системы на тактирование 84 МГц от PLL */
  SystemClock_Config();

  /* Настройка периферии */
  MX_GPIO_Init();
  MX_TIM5_Init();

  /* Установка заданного вариантом уровня газа (0 % -> импульс 42 мкс) */
  ESC_SetThrottle(THROTTLE_PERCENT);

  /* Запуск генерации ШИМ на канале TIM5_CH3 (вывод PA2) */
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3);

  /* Непрерывная выдача кадров OneShot42 выполняется аппаратно таймером,
     процессор в основном цикле не задействован */
  while (1)
  {
  }
}

/**
  * @brief  Настройка системы тактирования: HSI 16 МГц -> PLL -> SYSCLK 84 МГц.
  * @note   PLLM = 16, PLLN = 336, PLLP = 4:
  *           f(VCO in)  = 16 / 16 = 1 МГц;
  *           f(VCO out) = 1 * 336 = 336 МГц;
  *           SYSCLK     = 336 / 4 = 84 МГц.
  *         APB1 Prescaler = 2  ->  PCLK1 = 42 МГц, но тактовая частота
  *         таймеров шины APB1 удваивается, поэтому f(TIM5) = 84 МГц.
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /* Разрешение тактирования контроллера питания и выбор режима Scale 2 */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK |
                                     RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  Настройка TIM5 на режим ШИМ (PWM mode 1) по третьему каналу.
  */
static void MX_TIM5_Init(void)
{
  TIM_ClockConfigTypeDef  sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig      = {0};
  TIM_OC_InitTypeDef      sConfigOC          = {0};

  htim5.Instance               = TIM5;
  htim5.Init.Prescaler         = TIM5_PRESCALER;          /* PSC = 0  -> 84 МГц   */
  htim5.Init.CounterMode       = TIM_COUNTERMODE_UP;      /* счёт вверх            */
  htim5.Init.Period            = TIM5_PERIOD;             /* ARR = 10499 -> 125 мкс*/
  htim5.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }

  /* Источник тактирования счётчика - внутренний тактовый сигнал шины */
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim5, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_TIM_PWM_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* Режим PWM1: выход активен, пока CNT < CCR3 */
  sConfigOC.OCMode     = TIM_OCMODE_PWM1;
  sConfigOC.Pulse      = 0;                      /* реальное значение задаёт ESC_SetThrottle */
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;    /* активный уровень - высокий */
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim5, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  Настройка вывода PA2 как выхода третьего канала TIM5.
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_TIM5_CLK_ENABLE();

  GPIO_InitStruct.Pin       = GPIO_PIN_2;          /* PA2 = TIM5_CH3            */
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;     /* альтернативная функция    */
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH; /* короткие фронты     */
  GPIO_InitStruct.Alternate = GPIO_AF2_TIM5;       /* AF2 - функция TIM5        */
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
  * @brief  Обработчик неустранимых ошибок инициализации.
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
